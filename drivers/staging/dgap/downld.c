/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: downld.c,v 1.6 2009/01/14 14:10:54 markh Exp $
 */

/*
** downld.c
**
**  This is the daemon that sends the fep, bios, and concentrator images
**  from user space to the driver.
** BUGS: 
**  If the file changes in the middle of the download, you probably
**     will get what you deserve.
**
*/

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>

#include "dgap_types.h"
#include "digi.h"
#include "dgap_fep5.h"

#include "dgap_downld.h"

#include <string.h>
#include <malloc.h>
#include <stddef.h>
#include <unistd.h>

char		*pgm;
void		myperror();

/*
**  This structure is used to keep track of the different images available
**  to give to the driver.  It is arranged so that the things that are
**  constants or that have defaults are first inthe strucutre to simplify
**  the table of initializers.
*/
struct image_info {
	short	type;		/* bios, fep, conc */
	short	family;		/* boards this applies to */
	short	subtype;	/* subtype */
	int	len;		/* size of image */
	char	*image;		/* ioctl struct + image */
	char	*name;
	char	*fname;		/* filename of binary (i.e. "asfep.bin") */
	char	*pathname;	/* pathname to this binary ("/etc/dgap/xrfep.bin"); */
	time_t	mtime;		/* Last modification time */
};

#define IBIOS	0
#define	IFEP	1
#define	ICONC	2
#define ICONFIG	3
#define	IBAD	4

#define DEFAULT_LOC "/lib/firmware/dgap/"

struct image_info	*image_list;
int			nimages, count;

struct image_info images[] = {
{IBIOS, T_EPC,      SUBTYPE, 0, NULL, "EPC/X",	"fxbios.bin", DEFAULT_LOC "fxbios.bin", 0 },
{IFEP,  T_EPC,      SUBTYPE, 0, NULL, "EPC/X",	"fxfep.bin", DEFAULT_LOC "fxfep.bin", 0 },
{ICONC, T_EPC,      SUBTYPE, 0, NULL, "EPC/X",	"fxcon.bin", DEFAULT_LOC "fxcon.bin", 0 },

{IBIOS, T_CX,       SUBTYPE, 0, NULL, "C/X",	"cxbios.bin", DEFAULT_LOC "cxbios.bin", 0 },
{IFEP,  T_CX,       SUBTYPE, 0, NULL, "C/X",	"cxhost.bin", DEFAULT_LOC "cxhost.bin", 0 },

{IBIOS, T_CX,       T_PCIBUS, 0, NULL, "C/X PCI", "cxpbios.bin", DEFAULT_LOC "cxpbios.bin", 0 },
{IFEP,  T_CX,       T_PCIBUS, 0, NULL, "C/X PCI", "cxpfep.bin", DEFAULT_LOC "cxpfep.bin", 0 },

{ICONC, T_CX,       SUBTYPE, 0, NULL, "C/X",	"cxcon.bin", DEFAULT_LOC "cxcon.bin", 0 },
{ICONC, T_CX,       SUBTYPE, 0, NULL, "C/X",	"ibmcxcon.bin", DEFAULT_LOC "ibmcxcon.bin", 0 },
{ICONC, T_CX,       SUBTYPE, 0, NULL, "C/X",	"ibmencon.bin", DEFAULT_LOC "ibmencon.bin", 0 },

{IBIOS, FAMILY,   T_PCXR, 0, NULL, "PCXR",	"xrbios.bin", DEFAULT_LOC "xrbios.bin", 0 },
{IFEP,  FAMILY,   T_PCXR, 0,  NULL,  "PCXR",	"xrfep.bin", DEFAULT_LOC "xrfep.bin", 0  },

{IBIOS, T_PCLITE,   SUBTYPE, 0, NULL, "X/em",	"sxbios.bin", DEFAULT_LOC "sxbios.bin", 0 },
{IFEP,  T_PCLITE,   SUBTYPE, 0,  NULL,  "X/em",	"sxfep.bin", DEFAULT_LOC "sxfep.bin", 0  },

{IBIOS, T_EPC,      T_PCIBUS, 0, NULL, "PCI",	"pcibios.bin", DEFAULT_LOC "pcibios.bin", 0 },
{IFEP,  T_EPC,      T_PCIBUS, 0, NULL, "PCI",	"pcifep.bin", DEFAULT_LOC "pcifep.bin", 0 },
{ICONFIG, 0,	    0, 0, NULL,         NULL,	"dgap.conf",	"/etc/dgap.conf", 0 },

/* IBAD/NULL entry indicating end-of-table */

{IBAD,  0,     0, 0,  NULL,  NULL, NULL, NULL, 0 }

} ;

int 	errorprint = 1;
int 	nodldprint = 1;
int	debugflag;
int 	fd;

struct downld_t *ip;	/* Image pointer in current image  */
struct downld_t *dp; 	/* conc. download */


/*
 * The same for either the FEP or the BIOS. 
 *  Append the downldio header, issue the ioctl, then free
 *  the buffer.  Not horribly CPU efficient, but quite RAM efficient.
 */

void squirt(int req_type, int bdid, struct image_info *ii)
{
	struct downldio	*dliop;
	int size_buf;
	int sfd;
	struct stat sb;

	/*
	 * If this binary comes from a file, stat it to see how
	 * large it is. Yes, we intentionally do this each
	 * time for the binary may change between loads. 
	 */

	if (ii->pathname) {
		sfd = open(ii->pathname, O_RDONLY);

		if (sfd < 0 ) {
			myperror(ii->pathname);
			goto squirt_end; 
		}

		if (fstat(sfd, &sb) == -1 ) {
			myperror(ii->pathname);
			goto squirt_end;
		}

		ii->len = sb.st_size ; 
	}

	size_buf = ii->len + sizeof(struct downldio);

	/*
	 * This buffer will be freed at the end of this function.  It is
	 * not resilient and should be around only long enough for the d/l
	 * to happen.
	 */
	dliop = (struct downldio *) malloc(size_buf);

	if (dliop == NULL) {
		fprintf(stderr,"%s: can't get %d bytes of memory; aborting\n", 
			pgm, size_buf);
		exit (1);
	}

	/* Now, stick the image in fepimage.  This can come from either
	 *  the compiled-in image or from the filesystem.
	 */
	if (ii->pathname)
		read(sfd, dliop->image.fi.fepimage, ii->len);
	else
		memcpy(dliop ->image.fi.fepimage, ii->image, ii->len);

	dliop->req_type = req_type;
	dliop->bdid = bdid;

	dliop->image.fi.len = ii->len;

	if (debugflag)
		printf("sending %d bytes of %s %s from %s\n",
			ii->len, 
			(ii->type == IFEP) ? "FEP" : (ii->type == IBIOS) ? "BIOS" : "CONFIG",
			ii->name ? ii->name : "",
			(ii->pathname) ? ii->pathname : "internal image" );

	if (ioctl(fd, DIGI_DLREQ_SET, (char *) dliop) == -1) {
		if(errorprint) {
			fprintf(stderr,
				"%s: warning - download ioctl failed\n",pgm);
			errorprint = 0;
		}
		sleep(2);
	}

squirt_end:

	if (ii->pathname) {
		close(sfd);
	}
	free(dliop);
}


/*
 *  See if we need to reload the download image in core 
 * 
 */
void consider_file_rescan(struct image_info *ii)
{
	int sfd ; 
	int len ; 
	struct stat 	sb;

	/* This operation only makes sense when we're working from a file */

	if (ii->pathname) {

		sfd = open (ii->pathname, O_RDONLY) ;
		if (sfd < 0 ) {
			myperror(ii->pathname);
			exit(1) ;
		}

		if( fstat(sfd,&sb) == -1 ) {
			myperror(ii->pathname);
			exit(1);
		}
		
		/* If the file hasn't changed since we last did this, 
		 * and we have not done a free() on the image, bail  
		 */
		if (ii->image && (sb.st_mtime == ii->mtime))
			goto end_rescan;

		ii->len = len = sb.st_size ; 

		/* Record the timestamp of the file */
		ii->mtime = sb.st_mtime;

		/* image should be NULL unless there is an image malloced
		 * in already.  Before we malloc again, make sure we don't
		 * have a memory leak.
		 */
		if ( ii->image ) {
			free( ii->image ); 
			/* ii->image = NULL; */ /* not necessary */
		}

		/* This image will be kept only long enough for the 
		 * download to happen.  After sending the last block, 
		 * it will be freed
		 */
		ii->image = malloc(len) ;

		if (ii->image == NULL) {
			fprintf(stderr,
				"%s: can't get %d bytes of memory; aborting\n",
				 pgm, len);
			exit (1);
		}

		if (read(sfd, ii->image, len) < len) {
			fprintf(stderr,"%s: read error on %s; aborting\n", 
				pgm, ii->pathname);
			exit (1);
		}

end_rescan:
		close(sfd);
		
	}
}

/*
 * Scan for images to match the driver requests
 */

struct image_info * find_conc_image()
{
	int x ; 
	struct image_info *i = NULL ; 

	for ( x = 0; x < nimages; x++ ) {
		i=&image_list[x];
				
		if(i->type != ICONC)
			continue;

		consider_file_rescan(i) ;

		ip = (struct downld_t *) image_list[x].image;
		if (ip == NULL) continue;

		/*
		 * When I removed Clusterport, I kept only the code that I
		 * was SURE wasn't ClusterPort.  We may not need the next two
		 * lines of code.
		 */
		if ((dp->dl_type != 'P' ) && ( ip->dl_srev == dp->dl_srev ))
			return i;
	} 
	return NULL ; 
}


int main(int argc, char **argv)
{
	struct downldio	dlio;
	int 		offset, bsize;
	int 		x;
	char 		*down, *image, *fname;
	struct image_info *ii;

	pgm = argv[0];
	dp = &dlio.image.dl;		/* conc. download */

	while((argc > 2) && !strcmp(argv[1],"-d")) {
		debugflag++ ;
		argc-- ;
		argv++ ;
	}

	if(argc < 2) {
		fprintf(stderr,
			"usage: %s download-device [image-file] ...\n",
			pgm);
		exit(1);
	}



	/*
	 * Daemonize, unless debugging is turned on.
	 */
	if (debugflag == 0) {
		switch (fork())
		{
		case 0:
			break;

		case -1:
			return 1;

		default:
			return 0;
		}

		setsid();

		/*
		 * The child no longer needs "stdin", "stdout", or "stderr",
		 * and should not block processes waiting for them to close.
		 */
		fclose(stdin);
		fclose(stdout);
		fclose(stderr);

	}

	while (1) {
		if( (fd = open(argv[1], O_RDWR)) == -1 ) {
			sleep(1);
		}
		else
			break;
	}

	/*
	** create a list of images to search through when trying to match
	** requests from the driver.  Put images from the command line in
	** the list before built in images so that the command line images
	** can override the built in ones.
	*/
	
	/* allocate space for the list */

	nimages = argc - 2;

	/* count the number of default list entries */

	for (count = 0; images[count].type != IBAD; ++count) ;

	nimages += count;

	/* Really should just remove the variable "image_list".... robertl */
	image_list = images ; 
	   
	/* get the images from the command line */
	for(x = 2; x < argc; x++) {
		int xx; 

		/*
		 * strip off any leading path information for 
		 * determining file type 
		 */
		if( (fname = strrchr(argv[x],'/')) == NULL)
			fname = argv[x];
		else
			fname++;	/* skip the slash */

		for (xx = 0; xx < count; xx++) {
			if (strcmp(fname, images[xx].fname) == 0 ) { 
				images[xx].pathname = argv[x];

				/* image should be NULL until */
				/* space is malloced */
				images[xx].image = NULL ;  
			}
		}
	}

        sleep(3);
	
	/*
	** Endless loop: get a request from the fep, and service that request.
	*/
	for(;;) {
		/* get the request */
		if (debugflag)
			printf("b4 get ioctl...");
	
		if (ioctl(fd,DIGI_DLREQ_GET, &dlio) == -1 ) {
			if (errorprint) {
				fprintf(stderr,
					"%s: warning - download ioctl failed\n",
					pgm);
				errorprint = 0;
			}
			sleep(2);
		} else {
			if (debugflag)
				printf("dlio.req_type is %d bd %d\n",
					dlio.req_type,dlio.bdid);
	
			switch(dlio.req_type) {
			case DLREQ_BIOS:
				/*
				** find the bios image for this type
				*/
				for ( x = 0; x < nimages; x++ ) {
					if(image_list[x].type != IBIOS)
						continue;
	
					if ((dlio.image.fi.type & FAMILY) == 
						image_list[x].family) {
						
						if ( image_list[x].family == T_CX   ) { 
							if ((dlio.image.fi.type & BUSTYPE) 
								== T_PCIBUS ) {
								if ( image_list[x].subtype 
									== T_PCIBUS )
									break;
							}
							else { 
								break;
							}
						}
						else if ( image_list[x].family == T_EPC ) {
						/* If subtype of image is T_PCIBUS, it is */
						/* a PCI EPC image, so the board must */
						/* have bus type T_PCIBUS to match */
							if ((dlio.image.fi.type & BUSTYPE) 
								== T_PCIBUS ) {
								if ( image_list[x].subtype 
									== T_PCIBUS )
									break;
							}
							else { 
							/* NON PCI EPC doesn't use PCI image */
								if ( image_list[x].subtype 
									!= T_PCIBUS )
									break;
							}
						}
						else
							break;
					}
					else if ((dlio.image.fi.type & SUBTYPE) == image_list[x].subtype) {
						/* PCXR board will break out of the loop here */
						if ( image_list[x].subtype == T_PCXR   ) { 
									break;
						}
					}
				}
	
				if ( x >= nimages) {
					/*
					** no valid images exist
					*/
					if(nodldprint) {
						fprintf(stderr,
						"%s: cannot find correct BIOS image\n",
							pgm);
						nodldprint = 0;
					}
					dlio.image.fi.type = -1;
					if (ioctl(fd, DIGI_DLREQ_SET, &dlio) == -1) {
						if (errorprint) {
							fprintf(stderr,
							"%s: warning - download ioctl failed\n",
							pgm);
							errorprint = 0;
						}
						sleep(2);
					}
					break;
				}
				squirt(dlio.req_type, dlio.bdid, &image_list[x]);
				break ;
	
			case DLREQ_FEP:
				/*
				** find the fep image for this type
				*/
				for ( x = 0; x < nimages; x++ ) {
					if(image_list[x].type != IFEP)
						continue;
					if( (dlio.image.fi.type & FAMILY) == 
						image_list[x].family ) {
						if ( image_list[x].family == T_CX   ) { 
							/* C/X PCI board */
							if ((dlio.image.fi.type & BUSTYPE) 
								== T_PCIBUS ) {
								if ( image_list[x].subtype
									== T_PCIBUS )
									break;
							}
							else { 
							/* Regular CX */
								break;
							}
						}
						else if ( image_list[x].family == T_EPC   )  {
						/* If subtype of image is T_PCIBUS, it is */
						/* a PCI EPC image, so the board must */
						/* have bus type T_PCIBUS to match */
							if ((dlio.image.fi.type & BUSTYPE) 
								== T_PCIBUS ) {
								if ( image_list[x].subtype 
									== T_PCIBUS )
									break;
							}
							else { 
							/* NON PCI EPC doesn't use PCI image */
								if ( image_list[x].subtype 
									!= T_PCIBUS )
									break;
							}
						}
						else
							break;
					}
					else if ((dlio.image.fi.type & SUBTYPE) == image_list[x].subtype) {
						/* PCXR board will break out of the loop here */
						if ( image_list[x].subtype == T_PCXR   ) { 
									break;
						}
					}
				}
	
				if ( x >= nimages) {
					/*
					** no valid images exist
					*/
					if(nodldprint) {
						fprintf(stderr,
						"%s: cannot find correct FEP image\n",
							pgm);
						nodldprint = 0;
					}
					dlio.image.fi.type=-1;
					if( ioctl(fd,DIGI_DLREQ_SET,&dlio) == -1 ) {
						if(errorprint) {
							fprintf(stderr,
						"%s: warning - download ioctl failed\n",
								pgm);
							errorprint=0;
						}
						sleep(2);
					}
					break;
				}
				squirt(dlio.req_type, dlio.bdid, &image_list[x]);
				break;

			case DLREQ_DEVCREATE:
				{
					char string[1024];
#if 0
					sprintf(string, "%s /proc/dgap/%d/mknod", DEFSHELL, dlio.bdid);
#endif
					sprintf(string, "%s /usr/sbin/dgap_updatedevs %d", DEFSHELL, dlio.bdid);
					system(string);

					if (debugflag)
						printf("Created Devices.\n");
					if (ioctl(fd, DIGI_DLREQ_SET, &dlio) == -1 ) {
						if(errorprint) {
							fprintf(stderr, "%s: warning - DEVCREATE ioctl failed\n",pgm);
							errorprint = 0;
						}
						sleep(2);
					}
					if (debugflag)
						printf("After ioctl set - Created Device.\n");
				}

				break;
	
			case DLREQ_CONFIG:
				for ( x = 0; x < nimages; x++ ) {
					if(image_list[x].type != ICONFIG)
						continue;
					else
						break;
				}

				if ( x >= nimages) {
					/*
					** no valid images exist
					*/
					if(nodldprint) {
						fprintf(stderr,
						"%s: cannot find correct CONFIG image\n",
							pgm);
						nodldprint = 0;
					}
					dlio.image.fi.type=-1;
					if (ioctl(fd, DIGI_DLREQ_SET, &dlio) == -1 ) {
						if(errorprint) {
							fprintf(stderr,
						"%s: warning - download ioctl failed\n",
								pgm);
							errorprint=0;
						}
						sleep(2);
					}
					break;
				}

				squirt(dlio.req_type, dlio.bdid, &image_list[x]);
				break;

			case DLREQ_CONC:
				/*
				** find the image needed for this download
				*/
				if ( dp->dl_seq == 0 ) {
					/*
					** find image for hardware rev range
					*/
					for ( x = 0; x < nimages; x++ ) {
						ii=&image_list[x];
		
						if(image_list[x].type != ICONC)
							continue;
		
						consider_file_rescan(ii) ;
		
						ip = (struct downld_t *) image_list[x].image;
						if (ip == NULL) continue;
		
						/*
						 * When I removed Clusterport, I kept only the
						 * code that I was SURE wasn't ClusterPort.
						 * We may not need the next four lines of code.
						 */

						if ((dp->dl_type != 'P' ) &&
						 (ip->dl_lrev <= dp->dl_lrev ) && 
						 ( dp->dl_lrev <= ip->dl_hrev))
							break;
					}
				    
					if ( x >= nimages ) {
						/*
						** No valid images exist
						*/
						if(nodldprint) {
							fprintf(stderr,
						"%s: cannot find correct download image %d\n",
								pgm, dp->dl_lrev);
							nodldprint=0;
						}
						continue;
					}
				    
				} else {
					/*
					** find image version required
					*/
					if ((ii = find_conc_image()) == NULL ) {
						/*
						** No valid images exist
						*/
						fprintf(stderr,
						"%s: can't find rest of download image??\n",
							pgm);
						continue;
					}
				}
			
				/*
				** download block of image
				*/
			
				offset = 1024 * dp->dl_seq;
				
				/*
				** test if block requested within image
				*/
				if ( offset < ii->len ) { 
	
					/*
					** if it is, determine block size, set segment,
					** set size, set pointers, and copy block
					*/
					if (( bsize = ii->len - offset ) > 1024 )
						bsize = 1024;
				    
					/*
					** copy image version info to download area
					*/
					dp->dl_srev = ip->dl_srev;
					dp->dl_lrev = ip->dl_lrev;
					dp->dl_hrev = ip->dl_hrev;
				    
					dp->dl_seg = (64 * dp->dl_seq) + ip->dl_seg;
					dp->dl_size = bsize;
				    
					down = (char *)&dp->dl_data[0];
					image = (char *)((char *)ip + offset);
	
					memcpy(down, image, bsize);
				} 
				else {
					/*
					** Image has been downloaded, set segment and
					** size to indicate no more blocks
					*/
					dp->dl_seg = ip->dl_seg;
					dp->dl_size = 0;
	
					/* Now, we can release the concentrator */
					/* image from memory if we're running  */
					/* from filesystem images */
		
					if (ii->pathname)
						if (ii->image) {
							free(ii->image);
							ii->image = NULL ; 
						} 
				}
			
				if (debugflag)
						printf(
						"sending conc dl section %d to %s from %s\n",
							dp->dl_seq, ii->name,
						ii->pathname ? ii->pathname : "Internal Image");
		
				if (ioctl(fd, DIGI_DLREQ_SET, &dlio) == -1 ) {
					if (errorprint) {
						fprintf(stderr,
						"%s: warning - download ioctl failed\n",
							pgm);
						errorprint=0;
					}
					sleep(2);
				}
				break;
			} /* switch */
		}
		if (debugflag > 1) {
			printf("pausing: "); fflush(stdout);
			fflush(stdin);
			while(getchar() != '\n');
				printf("continuing\n");
		}
	}
}

/*
** myperror()
**
**  Same as normal perror(), but places the program name at the beginning
**  of the message.
*/
void myperror(char *s)
{
	fprintf(stderr,"%s: %s: %s.\n",pgm, s, strerror(errno));
}
