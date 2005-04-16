/* Linux driver for Philips webcam
   Decompression for chipset version 2 et 3
   (C) 2004      Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "pwc-timon.h"
#include "pwc-kiara.h"
#include "pwc-dec23.h"
#include "pwc-ioctl.h"

#include <linux/string.h>

/****
 *
 *
 *
 */


static void fill_table_a000(unsigned int *p)
{
  static unsigned int initial_values[12] = {
     0xFFAD9B00, 0xFFDDEE00, 0x00221200, 0x00526500,
     0xFFC21E00, 0x003DE200, 0xFF924B80, 0xFFD2A300,
     0x002D5D00, 0x006DB480, 0xFFED3E00, 0x0012C200
  };
  static unsigned int values_derivated[12] = {
     0x0000A4CA, 0x00004424, 0xFFFFBBDC, 0xFFFF5B36,
     0x00007BC4, 0xFFFF843C, 0x0000DB69, 0x00005ABA,
     0xFFFFA546, 0xFFFF2497, 0x00002584, 0xFFFFDA7C
  };
  unsigned int temp_values[12];
  int i,j;

  memcpy(temp_values,initial_values,sizeof(initial_values));
  for (i=0;i<256;i++)
   {
     for (j=0;j<12;j++)
      {
	*p++ = temp_values[j];
	temp_values[j] += values_derivated[j];
      }
   }
}

static void fill_table_d000(unsigned char *p)
{
  int bit,byte;

  for (bit=0; bit<8; bit++)
   {
     unsigned char bitpower = 1<<bit;
     unsigned char mask = bitpower-1;
     for (byte=0; byte<256; byte++)
      {
	if (byte & bitpower)
	  *p++ = -(byte & mask);
	else
	  *p++ = (byte & mask);
      }
   }
}

/*
 *
 * Kiara: 0 <= ver <= 7
 * Timon: 0 <= ver <= 15
 *
 */
static void fill_table_color(unsigned int version, const unsigned int *romtable, 
    unsigned char *p0004, 
    unsigned char *p8004)
{
  const unsigned int *table;
  unsigned char *p0, *p8;
  int i,j,k;
  int dl,bit,pw;

  romtable += version*256;

  for (i=0; i<2; i++)
   {
     table = romtable + i*128;

     for (dl=0; dl<16; dl++)
      {
	p0 = p0004 + (i<<14) + (dl<<10);
	p8 = p8004 + (i<<12) + (dl<<8);

	for (j=0; j<8; j++ , table++, p0+=128)
	 {
	   for (k=0; k<16; k++)
	    {
	      if (k==0)
		bit=1;
	      else if (k>=1 && k<3)
		bit=(table[0]>>15)&7;
	      else if (k>=3 && k<6)
		bit=(table[0]>>12)&7;
	      else if (k>=6 && k<10)
		bit=(table[0]>>9)&7;
	      else if (k>=10 && k<13)
		bit=(table[0]>>6)&7;
	      else if (k>=13 && k<15)
		bit=(table[0]>>3)&7;
	      else
		bit=(table[0])&7;
	      if (k == 0)
		*(unsigned char *)p8++ = 8;
	      else
		*(unsigned char *)p8++ = j - bit;
	      *(unsigned char *)p8++ = bit;

	      pw = 1<<bit;
	      p0[k+0x00] = (1*pw)  + 0x80;
	      p0[k+0x10] = (2*pw)  + 0x80;
	      p0[k+0x20] = (3*pw)  + 0x80;
	      p0[k+0x30] = (4*pw)  + 0x80;
	      p0[k+0x40] = (-pw)   + 0x80;
	      p0[k+0x50] = (2*-pw) + 0x80;
	      p0[k+0x60] = (3*-pw) + 0x80;
	      p0[k+0x70] = (4*-pw) + 0x80;
	    } /* end of for (k=0; k<16; k++, p8++) */
	 } /* end of for (j=0; j<8; j++ , table++) */
      } /* end of for (dl=0; dl<16; dl++) */
   } /* end of for (i=0; i<2; i++) */
}

/*
 * precision = (pdev->xx + pdev->yy)
 *
 */
static void fill_table_dc00_d800(unsigned int precision, unsigned int *pdc00, unsigned int *pd800)
{
  int i;
  unsigned int offset1, offset2;
 
  for(i=0,offset1=0x4000, offset2=0; i<256 ; i++,offset1+=0x7BC4, offset2+=0x7BC4)
   {
     unsigned int msb = offset1 >> 15;

     if ( msb > 255)
      {
	if (msb)
	  msb=0;
	else
	  msb=255;
      }

     *pdc00++ = msb << precision;
     *pd800++ = offset2;
   }

}

/*
 * struct {
 *   unsigned char op;	    // operation to execute
 *   unsigned char bits;    // bits use to perform operation
 *   unsigned char offset1; // offset to add to access in the table_0004 % 16
 *   unsigned char offset2; // offset to add to access in the table_0004
 * }
 *
 */
static unsigned int table_ops[] = {
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x10, 0x00,0x06,0x01,0x30,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x01,0x20, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x50, 0x00,0x05,0x02,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x03,0x00, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x10, 0x00,0x06,0x02,0x10,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x01,0x60, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x50, 0x00,0x05,0x02,0x40,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x03,0x40, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x10, 0x00,0x06,0x01,0x70,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x01,0x20, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x50, 0x00,0x05,0x02,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x03,0x00, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x10, 0x00,0x06,0x02,0x50,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x01,0x60, 0x01,0x00,0x00,0x00,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x00, 0x00,0x04,0x01,0x50, 0x00,0x05,0x02,0x40,
0x02,0x00,0x00,0x00, 0x00,0x03,0x01,0x40, 0x00,0x05,0x03,0x40, 0x01,0x00,0x00,0x00
};

/*
 * TODO: multiply by 4 all values
 *
 */
static unsigned int MulIdx[256] = {
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4,
 6, 7, 8, 9, 7,10,11, 8, 8,11,10, 7, 9, 8, 7, 6,
 4, 5, 5, 4, 4, 5, 5, 4, 4, 5, 5, 4, 4, 5, 5, 4,
 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 0, 2,
 0, 3, 3, 0, 1, 2, 2, 1, 2, 1, 1, 2, 3, 0, 0, 3,
 0, 1, 2, 3, 3, 2, 1, 0, 3, 2, 1, 0, 0, 1, 2, 3,
 1, 1, 1, 1, 3, 3, 3, 3, 0, 0, 0, 0, 2, 2, 2, 2,
 7,10,11, 8, 9, 8, 7, 6, 6, 7, 8, 9, 8,11,10, 7,
 4, 5, 5, 4, 5, 4, 4, 5, 5, 4, 4, 5, 4, 5, 5, 4,
 7, 9, 6, 8,10, 8, 7,11,11, 7, 8,10, 8, 6, 9, 7,
 1, 3, 0, 2, 2, 0, 3, 1, 2, 0, 3, 1, 1, 3, 0, 2,
 1, 2, 2, 1, 3, 0, 0, 3, 0, 3, 3, 0, 2, 1, 1, 2,
10, 8, 7,11, 8, 6, 9, 7, 7, 9, 6, 8,11, 7, 8,10
};



void pwc_dec23_init(int type, int release, unsigned char *mode, void *data)
{
  int flags;
  struct pwc_dec23_private *pdev = data;
  release = release;

  switch (type)
   {
    case 720:
    case 730:
    case 740:
    case 750:
      flags = mode[2]&0x18;	/* our: flags = 8, mode[2]==e8 */
      if (flags==8)
	pdev->zz = 7;
      else if (flags==0x10)
	pdev->zz = 8;
      else
	pdev->zz = 6;
      flags = mode[2]>>5;	/* our: 7 */

      fill_table_color(flags, (unsigned int *)KiaraRomTable, pdev->table_0004, pdev->table_8004);
      break;


    case 675:
    case 680:
    case 690:
      flags = mode[2]&6;
      if (flags==2)
	pdev->zz = 7;
      else if (flags==4)
	pdev->zz = 8;
      else
	pdev->zz = 6;
      flags = mode[2]>>3;

      fill_table_color(flags, (unsigned int *)TimonRomTable, pdev->table_0004, pdev->table_8004);
      break;

    default:
      /* Not supported */
      return;
   }

  /* * * * ** */
  pdev->xx = 8 - pdev->zz;
  pdev->yy = 15 - pdev->xx;
  pdev->zzmask = 0xFF>>pdev->xx;
  //pdev->zzmask = (1U<<pdev->zz)-1;


  fill_table_dc00_d800(pdev->xx + pdev->yy, pdev->table_dc00, pdev->table_d800);
  fill_table_a000(pdev->table_a004);
  fill_table_d000(pdev->table_d004);
}


/*
 * To manage the stream, we keep in a 32 bits variables,
 * the next bits in the stream. fill_reservoir() add to
 * the reservoir at least wanted nbits.
 *
 *
 */
#define fill_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted) do { \
   while (nbits_in_reservoir<nbits_wanted) \
    { \
      reservoir |= (*(stream)++) << nbits_in_reservoir; \
      nbits_in_reservoir+=8; \
    } \
}  while(0);

#define get_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted,result) do { \
   fill_nbits(reservoir,nbits_in_reservoir,stream,nbits_wanted); \
   result = (reservoir) & ((1U<<nbits_wanted)-1); \
   reservoir >>= nbits_wanted; \
   nbits_in_reservoir -= nbits_wanted; \
}  while(0);



static void DecompressBand23(const struct pwc_dec23_private *pdev,
                             const unsigned char *rawyuv,
			     unsigned char *planar_y,
			     unsigned char *planar_u,
			     unsigned char *planar_v,
			     unsigned int image_x,		/* aka number of pixels wanted ??? */
			     unsigned int pixels_per_line,	/* aka number of pixels per line */
			     int flags)
{


  unsigned int reservoir, nbits_in_reservoir;
  int first_4_bits;
  unsigned int bytes_per_channel;
  int line_size;	/* size of the line (4Y+U+V) */
  int passes;
  const unsigned char *ptable0004, *ptable8004;

  int even_line;
  unsigned int temp_colors[16];
  int nblocks;

  const unsigned char *stream;
  unsigned char *dest_y, *dest_u=NULL, *dest_v=NULL;
  unsigned int offset_to_plane_u, offset_to_plane_v;

  int i;


  reservoir = 0;
  nbits_in_reservoir = 0;
  stream = rawyuv+1;	/* The first byte of the stream is skipped */
  even_line = 1;

  get_nbits(reservoir,nbits_in_reservoir,stream,4,first_4_bits);

  line_size = pixels_per_line*3;

  for (passes=0;passes<2;passes++)
   {
     if (passes==0)
      {
	bytes_per_channel = pixels_per_line;
	dest_y = planar_y;
	nblocks = image_x/4;
      }
     else
      {
	/* Format planar: All Y, then all U, then all V */
	bytes_per_channel = pixels_per_line/2;
	dest_u = planar_u;
	dest_v = planar_v;
	dest_y = dest_u;
	nblocks = image_x/8;
      }

     offset_to_plane_u = bytes_per_channel*2;
     offset_to_plane_v = bytes_per_channel*3;
     /*
     printf("bytes_per_channel = %d\n",bytes_per_channel);
     printf("offset_to_plane_u = %d\n",offset_to_plane_u);
     printf("offset_to_plane_v = %d\n",offset_to_plane_v);
     */

     while (nblocks-->0)
      {
	unsigned int gray_index;

	fill_nbits(reservoir,nbits_in_reservoir,stream,16);
	gray_index = reservoir & pdev->zzmask;
	reservoir >>= pdev->zz;
	nbits_in_reservoir -= pdev->zz;

	fill_nbits(reservoir,nbits_in_reservoir,stream,2);

	if ( (reservoir & 3) == 0)
	 {
	   reservoir>>=2;
	   nbits_in_reservoir-=2;
	   for (i=0;i<16;i++)
	     temp_colors[i] = pdev->table_dc00[gray_index];

	 }
	else
	 {
	   unsigned int channel_v, offset1;

	   /* swap bit 0 and 2 of offset_OR */
	   channel_v = ((reservoir & 1) << 2) | (reservoir & 2) | ((reservoir & 4)>>2);
	   reservoir>>=3;
	   nbits_in_reservoir-=3;

	   for (i=0;i<16;i++)
	     temp_colors[i] = pdev->table_d800[gray_index];

	   ptable0004 = pdev->table_0004 + (passes*16384) + (first_4_bits*1024) + (channel_v*128);
	   ptable8004 = pdev->table_8004 + (passes*4096)  + (first_4_bits*256)  + (channel_v*32);

	   offset1 = 0;
	   while(1) 
	    {
	      unsigned int index_in_table_ops, op, rows=0;
	      fill_nbits(reservoir,nbits_in_reservoir,stream,16);

	      /* mode is 0,1 or 2 */
	      index_in_table_ops = (reservoir&0x3F);
	      op = table_ops[ index_in_table_ops*4 ];
	      if (op == 2)
	       {
		 reservoir >>= 2;
		 nbits_in_reservoir -= 2;
		 break;	/* exit the while(1) */
	       }
	      if (op == 0)
	       {
		 unsigned int shift;

		 offset1 = (offset1 + table_ops[index_in_table_ops*4+2]) & 0x0F;
		 shift = table_ops[ index_in_table_ops*4+1 ];
		 reservoir >>= shift;
		 nbits_in_reservoir -= shift;
		 rows = ptable0004[ offset1 + table_ops[index_in_table_ops*4+3] ];
	       }
	      if (op == 1)
	       {
		  /* 10bits [ xxxx xxxx yyyy 000 ]
		   * yyy => offset in the table8004
		   * xxx => offset in the tabled004
		   */
		 unsigned int mask, shift;
		 unsigned int col1, row1, total_bits;

		 offset1 = (offset1 + ((reservoir>>3)&0x0F)+1) & 0x0F;

		 col1 = (reservoir>>7) & 0xFF;
		 row1 = ptable8004 [ offset1*2 ];

		 /* Bit mask table */
		 mask = pdev->table_d004[ (row1<<8) + col1 ];
		 shift = ptable8004 [ offset1*2 + 1];
		 rows = ((mask << shift) + 0x80) & 0xFF;

		 total_bits = row1 + 8;
		 reservoir >>= total_bits;
		 nbits_in_reservoir -= total_bits;
	       }
	       {
		 const unsigned int *table_a004 = pdev->table_a004 + rows*12;
		 unsigned int *poffset = MulIdx + offset1*16;	/* 64/4 (int) */
		 for (i=0;i<16;i++)
		  {
		    temp_colors[i] += table_a004[ *poffset ];
		    poffset++;
		  }
	       }
	   }
	 }
#define USE_SIGNED_INT_FOR_COLOR
#ifdef USE_SIGNED_INT_FOR_COLOR
#  define CLAMP(x) ((x)>255?255:((x)<0?0:x))
#else
#  define CLAMP(x) ((x)>255?255:x)
#endif

	if (passes == 0)
	 {
#ifdef USE_SIGNED_INT_FOR_COLOR
	   const int *c = temp_colors;
#else
	   const unsigned int *c = temp_colors;
#endif
	   unsigned char *d;

	   d = dest_y;
	   for (i=0;i<4;i++,c++)
	     *d++ = CLAMP((*c) >> pdev->yy);

	   d = dest_y + bytes_per_channel;
	   for (i=0;i<4;i++,c++)
	     *d++ = CLAMP((*c) >> pdev->yy);

	   d = dest_y + offset_to_plane_u;
	   for (i=0;i<4;i++,c++)
	     *d++ = CLAMP((*c) >> pdev->yy);

	   d = dest_y + offset_to_plane_v;
	   for (i=0;i<4;i++,c++)
	     *d++ = CLAMP((*c) >> pdev->yy);

	   dest_y += 4;
	 }
	else if (passes == 1)
	 {
#ifdef USE_SIGNED_INT_FOR_COLOR
	   int *c1 = temp_colors;
	   int *c2 = temp_colors+4;
#else
	   unsigned int *c1 = temp_colors;
	   unsigned int *c2 = temp_colors+4;
#endif
	   unsigned char *d;

	   d = dest_y;
	   for (i=0;i<4;i++,c1++,c2++)
	    {
	      *d++ = CLAMP((*c1) >> pdev->yy);
	      *d++ = CLAMP((*c2) >> pdev->yy);
	    }
	   c1 = temp_colors+12;
	   //c2 = temp_colors+8;
	   d = dest_y + bytes_per_channel;
	   for (i=0;i<4;i++,c1++,c2++)
	    {
	      *d++ = CLAMP((*c1) >> pdev->yy);
	      *d++ = CLAMP((*c2) >> pdev->yy);
	    }

	   if (even_line)	/* Each line, swap u/v */
	    {
	      even_line=0;
	      dest_y = dest_v;
	      dest_u += 8;
	    }
	   else
	    {
	      even_line=1;
	      dest_y = dest_u;
	      dest_v += 8;
	    }
	 }

      } /* end of while (nblocks-->0) */

   } /* end of for (passes=0;passes<2;passes++) */

}


/**
 *
 * image: size of the image wanted
 * view : size of the image returned by the camera
 * offset: (x,y) to displayer image in the view
 *
 * src: raw data
 * dst: image output
 * flags: PWCX_FLAG_PLANAR
 * pdev: private buffer
 * bandlength:
 *
 */
void pwc_dec23_decompress(const struct pwc_coord *image,
                            const struct pwc_coord *view,
			    const struct pwc_coord *offset,
			    const void *src,
			    void *dst,
			    int flags,
			    const void *data,
			    int bandlength)
{
  const struct pwc_dec23_private *pdev = data;
  unsigned char *pout, *pout_planar_y=NULL, *pout_planar_u=NULL, *pout_planar_v=NULL;
  int i,n,stride,pixel_size;


  if (flags & PWCX_FLAG_BAYER)
   {
     pout = dst + (view->x * offset->y) + offset->x;
     pixel_size = view->x * 4;
   }
  else
   {
     n = view->x * view->y;

     /* offset in Y plane */
     stride = view->x * offset->y;
     pout_planar_y = dst + stride + offset->x;

     /* offsets in U/V planes */
     stride = (view->x * offset->y)/4 + offset->x/2;
     pout_planar_u = dst + n +     + stride;
     pout_planar_v = dst + n + n/4 + stride;

     pixel_size = view->x * 4;
   }


  for (i=0;i<image->y;i+=4)
   {
     if (flags & PWCX_FLAG_BAYER)
      {
	//TODO:
	//DecompressBandBayer(pdev,src,pout,image.x,view->x,flags);
	src += bandlength;
	pout += pixel_size;
      }
     else
      {
	DecompressBand23(pdev,src,pout_planar_y,pout_planar_u,pout_planar_v,image->x,view->x,flags);
	src += bandlength;
	pout_planar_y += pixel_size;
	pout_planar_u += view->x;
	pout_planar_v += view->x;
      }
   }
}

void pwc_dec23_exit(void)
{
  /* Do nothing */

}

