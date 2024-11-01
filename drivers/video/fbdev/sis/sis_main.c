// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SiS 300/540/630[S]/730[S],
 * SiS 315[E|PRO]/550/[M]65x/[M]66x[F|M|G]X/[M]74x[GX]/330/[M]76x[GX],
 * XGI V3XT/V5/V8, Z7
 * frame buffer driver for Linux kernels >= 2.4.14 and >=2.6.3
 *
 * Copyright (C) 2001-2005 Thomas Winischhofer, Vienna, Austria.
 *
 * Author:	Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Author of (practically wiped) code base:
 *		SiS (www.sis.com)
 *		Copyright (C) 1999 Silicon Integrated Systems, Inc.
 *
 * See http://www.winischhofer.net/ for more information and updates
 *
 * Originally based on the VBE 2.0 compliant graphic boards framebuffer driver,
 * which is (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 */

#include <linux/aperture.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/screen_info.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#include "sis.h"
#include "sis_main.h"
#include "init301.h"

#if !defined(CONFIG_FB_SIS_300) && !defined(CONFIG_FB_SIS_315)
#warning Neither CONFIG_FB_SIS_300 nor CONFIG_FB_SIS_315 is set
#warning sisfb will not work!
#endif

/* ---------------------- Prototypes ------------------------- */

/* Interface used by the world */
#ifndef MODULE
static int sisfb_setup(char *options);
#endif

/* Interface to the low level console driver */
static int sisfb_init(void);

/* fbdev routines */
static int	sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
				struct fb_info *info);

static int	sisfb_ioctl(struct fb_info *info, unsigned int cmd,
			    unsigned long arg);
static int	sisfb_set_par(struct fb_info *info);
static int	sisfb_blank(int blank,
				struct fb_info *info);

static void sisfb_handle_command(struct sis_video_info *ivideo,
				 struct sisfb_cmd *sisfb_command);

static void	sisfb_search_mode(char *name, bool quiet);
static int	sisfb_validate_mode(struct sis_video_info *ivideo, int modeindex, u32 vbflags);
static u8	sisfb_search_refresh_rate(struct sis_video_info *ivideo, unsigned int rate,
				int index);
static int	sisfb_setcolreg(unsigned regno, unsigned red, unsigned green,
				unsigned blue, unsigned transp,
				struct fb_info *fb_info);
static int	sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
				struct fb_info *info);
static void	sisfb_pre_setmode(struct sis_video_info *ivideo);
static void	sisfb_post_setmode(struct sis_video_info *ivideo);
static bool	sisfb_CheckVBRetrace(struct sis_video_info *ivideo);
static bool	sisfbcheckvretracecrt2(struct sis_video_info *ivideo);
static bool	sisfbcheckvretracecrt1(struct sis_video_info *ivideo);
static bool	sisfb_bridgeisslave(struct sis_video_info *ivideo);
static void	sisfb_detect_VB_connect(struct sis_video_info *ivideo);
static void	sisfb_get_VB_type(struct sis_video_info *ivideo);
static void	sisfb_set_TVxposoffset(struct sis_video_info *ivideo, int val);
static void	sisfb_set_TVyposoffset(struct sis_video_info *ivideo, int val);

/* Internal heap routines */
static int		sisfb_heap_init(struct sis_video_info *ivideo);
static struct SIS_OH *	sisfb_poh_new_node(struct SIS_HEAP *memheap);
static struct SIS_OH *	sisfb_poh_allocate(struct SIS_HEAP *memheap, u32 size);
static void		sisfb_delete_node(struct SIS_OH *poh);
static void		sisfb_insert_node(struct SIS_OH *pohList, struct SIS_OH *poh);
static struct SIS_OH *	sisfb_poh_free(struct SIS_HEAP *memheap, u32 base);
static void		sisfb_free_node(struct SIS_HEAP *memheap, struct SIS_OH *poh);


/* ------------------ Internal helper routines ----------------- */

static void __init
sisfb_setdefaultparms(void)
{
	sisfb_off		= 0;
	sisfb_parm_mem		= 0;
	sisfb_accel		= -1;
	sisfb_ypan		= -1;
	sisfb_max		= -1;
	sisfb_userom		= -1;
	sisfb_useoem		= -1;
	sisfb_mode_idx		= -1;
	sisfb_parm_rate		= -1;
	sisfb_crt1off		= 0;
	sisfb_forcecrt1		= -1;
	sisfb_crt2type		= -1;
	sisfb_crt2flags		= 0;
	sisfb_pdc		= 0xff;
	sisfb_pdca		= 0xff;
	sisfb_scalelcd		= -1;
	sisfb_specialtiming 	= CUT_NONE;
	sisfb_lvdshl		= -1;
	sisfb_dstn		= 0;
	sisfb_fstn		= 0;
	sisfb_tvplug		= -1;
	sisfb_tvstd		= -1;
	sisfb_tvxposoffset	= 0;
	sisfb_tvyposoffset	= 0;
	sisfb_nocrt2rate	= 0;
#if !defined(__i386__) && !defined(__x86_64__)
	sisfb_resetcard		= 0;
	sisfb_videoram		= 0;
#endif
}

/* ------------- Parameter parsing -------------- */

static void sisfb_search_vesamode(unsigned int vesamode, bool quiet)
{
	int i = 0, j = 0;

	/* We don't know the hardware specs yet and there is no ivideo */

	if(vesamode == 0) {
		if(!quiet)
			printk(KERN_ERR "sisfb: Invalid mode. Using default.\n");

		sisfb_mode_idx = DEFAULT_MODE;

		return;
	}

	vesamode &= 0x1dff;  /* Clean VESA mode number from other flags */

	while(sisbios_mode[i++].mode_no[0] != 0) {
		if( (sisbios_mode[i-1].vesa_mode_no_1 == vesamode) ||
		    (sisbios_mode[i-1].vesa_mode_no_2 == vesamode) ) {
			if(sisfb_fstn) {
				if(sisbios_mode[i-1].mode_no[1] == 0x50 ||
				   sisbios_mode[i-1].mode_no[1] == 0x56 ||
				   sisbios_mode[i-1].mode_no[1] == 0x53)
					continue;
			} else {
				if(sisbios_mode[i-1].mode_no[1] == 0x5a ||
				   sisbios_mode[i-1].mode_no[1] == 0x5b)
					continue;
			}
			sisfb_mode_idx = i - 1;
			j = 1;
			break;
		}
	}
	if((!j) && !quiet)
		printk(KERN_ERR "sisfb: Invalid VESA mode 0x%x'\n", vesamode);
}

static void sisfb_search_mode(char *name, bool quiet)
{
	unsigned int j = 0, xres = 0, yres = 0, depth = 0, rate = 0;
	int i = 0;
	char strbuf[16], strbuf1[20];
	char *nameptr = name;

	/* We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL) {
		if(!quiet)
			printk(KERN_ERR "sisfb: Internal error, using default mode.\n");

		sisfb_mode_idx = DEFAULT_MODE;
		return;
	}

	if(!strncasecmp(name, sisbios_mode[MODE_INDEX_NONE].name, strlen(name))) {
		if(!quiet)
			printk(KERN_ERR "sisfb: Mode 'none' not supported anymore. Using default.\n");

		sisfb_mode_idx = DEFAULT_MODE;
		return;
	}

	if(strlen(name) <= 19) {
		strcpy(strbuf1, name);
		for(i = 0; i < strlen(strbuf1); i++) {
			if(strbuf1[i] < '0' || strbuf1[i] > '9') strbuf1[i] = ' ';
		}

		/* This does some fuzzy mode naming detection */
		if(sscanf(strbuf1, "%u %u %u %u", &xres, &yres, &depth, &rate) == 4) {
			if((rate <= 32) || (depth > 32)) {
				swap(rate, depth);
			}
			sprintf(strbuf, "%ux%ux%u", xres, yres, depth);
			nameptr = strbuf;
			sisfb_parm_rate = rate;
		} else if(sscanf(strbuf1, "%u %u %u", &xres, &yres, &depth) == 3) {
			sprintf(strbuf, "%ux%ux%u", xres, yres, depth);
			nameptr = strbuf;
		} else {
			xres = 0;
			if((sscanf(strbuf1, "%u %u", &xres, &yres) == 2) && (xres != 0)) {
				sprintf(strbuf, "%ux%ux8", xres, yres);
				nameptr = strbuf;
			} else {
				sisfb_search_vesamode(simple_strtoul(name, NULL, 0), quiet);
				return;
			}
		}
	}

	i = 0; j = 0;
	while(sisbios_mode[i].mode_no[0] != 0) {
		if(!strncasecmp(nameptr, sisbios_mode[i++].name, strlen(nameptr))) {
			if(sisfb_fstn) {
				if(sisbios_mode[i-1].mode_no[1] == 0x50 ||
				   sisbios_mode[i-1].mode_no[1] == 0x56 ||
				   sisbios_mode[i-1].mode_no[1] == 0x53)
					continue;
			} else {
				if(sisbios_mode[i-1].mode_no[1] == 0x5a ||
				   sisbios_mode[i-1].mode_no[1] == 0x5b)
					continue;
			}
			sisfb_mode_idx = i - 1;
			j = 1;
			break;
		}
	}

	if((!j) && !quiet)
		printk(KERN_ERR "sisfb: Invalid mode '%s'\n", nameptr);
}

#ifndef MODULE
static void sisfb_get_vga_mode_from_kernel(void)
{
#ifdef CONFIG_X86
	char mymode[32];
	int  mydepth = screen_info.lfb_depth;

	if(screen_info.orig_video_isVGA != VIDEO_TYPE_VLFB) return;

	if( (screen_info.lfb_width >= 320) && (screen_info.lfb_width <= 2048) &&
	    (screen_info.lfb_height >= 200) && (screen_info.lfb_height <= 1536) &&
	    (mydepth >= 8) && (mydepth <= 32) ) {

		if(mydepth == 24) mydepth = 32;

		sprintf(mymode, "%ux%ux%u", screen_info.lfb_width,
					screen_info.lfb_height,
					mydepth);

		printk(KERN_DEBUG
			"sisfb: Using vga mode %s pre-set by kernel as default\n",
			mymode);

		sisfb_search_mode(mymode, true);
	}
#endif
	return;
}
#endif

static void __init
sisfb_search_crt2type(const char *name)
{
	int i = 0;

	/* We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL) return;

	while(sis_crt2type[i].type_no != -1) {
		if(!strncasecmp(name, sis_crt2type[i].name, strlen(sis_crt2type[i].name))) {
			sisfb_crt2type = sis_crt2type[i].type_no;
			sisfb_tvplug = sis_crt2type[i].tvplug_no;
			sisfb_crt2flags = sis_crt2type[i].flags;
			break;
		}
		i++;
	}

	sisfb_dstn = (sisfb_crt2flags & FL_550_DSTN) ? 1 : 0;
	sisfb_fstn = (sisfb_crt2flags & FL_550_FSTN) ? 1 : 0;

	if(sisfb_crt2type < 0)
		printk(KERN_ERR "sisfb: Invalid CRT2 type: %s\n", name);
}

static void __init
sisfb_search_tvstd(const char *name)
{
	int i = 0;

	/* We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL)
		return;

	while(sis_tvtype[i].type_no != -1) {
		if(!strncasecmp(name, sis_tvtype[i].name, strlen(sis_tvtype[i].name))) {
			sisfb_tvstd = sis_tvtype[i].type_no;
			break;
		}
		i++;
	}
}

static void __init
sisfb_search_specialtiming(const char *name)
{
	int i = 0;
	bool found = false;

	/* We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL)
		return;

	if(!strncasecmp(name, "none", 4)) {
		sisfb_specialtiming = CUT_FORCENONE;
		printk(KERN_DEBUG "sisfb: Special timing disabled\n");
	} else {
		while(mycustomttable[i].chipID != 0) {
			if(!strncasecmp(name,mycustomttable[i].optionName,
			   strlen(mycustomttable[i].optionName))) {
				sisfb_specialtiming = mycustomttable[i].SpecialID;
				found = true;
				printk(KERN_INFO "sisfb: Special timing for %s %s forced (\"%s\")\n",
					mycustomttable[i].vendorName,
					mycustomttable[i].cardName,
					mycustomttable[i].optionName);
				break;
			}
			i++;
		}
		if(!found) {
			printk(KERN_WARNING "sisfb: Invalid SpecialTiming parameter, valid are:");
			printk(KERN_WARNING "\t\"none\" (to disable special timings)\n");
			i = 0;
			while(mycustomttable[i].chipID != 0) {
				printk(KERN_WARNING "\t\"%s\" (for %s %s)\n",
					mycustomttable[i].optionName,
					mycustomttable[i].vendorName,
					mycustomttable[i].cardName);
				i++;
			}
		}
	}
}

/* ----------- Various detection routines ----------- */

static void sisfb_detect_custom_timing(struct sis_video_info *ivideo)
{
	unsigned char *biosver = NULL;
	unsigned char *biosdate = NULL;
	bool footprint;
	u32 chksum = 0;
	int i, j;

	if(ivideo->SiS_Pr.UseROM) {
		biosver = ivideo->SiS_Pr.VirtualRomBase + 0x06;
		biosdate = ivideo->SiS_Pr.VirtualRomBase + 0x2c;
		for(i = 0; i < 32768; i++)
			chksum += ivideo->SiS_Pr.VirtualRomBase[i];
	}

	i = 0;
	do {
		if( (mycustomttable[i].chipID == ivideo->chip)			&&
		    ((!strlen(mycustomttable[i].biosversion)) ||
		     (ivideo->SiS_Pr.UseROM &&
		      (!strncmp(mycustomttable[i].biosversion, biosver,
				strlen(mycustomttable[i].biosversion)))))	&&
		    ((!strlen(mycustomttable[i].biosdate)) ||
		     (ivideo->SiS_Pr.UseROM &&
		      (!strncmp(mycustomttable[i].biosdate, biosdate,
				strlen(mycustomttable[i].biosdate)))))		&&
		    ((!mycustomttable[i].bioschksum) ||
		     (ivideo->SiS_Pr.UseROM &&
		      (mycustomttable[i].bioschksum == chksum)))		&&
		    (mycustomttable[i].pcisubsysvendor == ivideo->subsysvendor) &&
		    (mycustomttable[i].pcisubsyscard == ivideo->subsysdevice) ) {
			footprint = true;
			for(j = 0; j < 5; j++) {
				if(mycustomttable[i].biosFootprintAddr[j]) {
					if(ivideo->SiS_Pr.UseROM) {
						if(ivideo->SiS_Pr.VirtualRomBase[mycustomttable[i].biosFootprintAddr[j]] !=
							mycustomttable[i].biosFootprintData[j]) {
							footprint = false;
						}
					} else
						footprint = false;
				}
			}
			if(footprint) {
				ivideo->SiS_Pr.SiS_CustomT = mycustomttable[i].SpecialID;
				printk(KERN_DEBUG "sisfb: Identified [%s %s], special timing applies\n",
					mycustomttable[i].vendorName,
				mycustomttable[i].cardName);
				printk(KERN_DEBUG "sisfb: [specialtiming parameter name: %s]\n",
					mycustomttable[i].optionName);
				break;
			}
		}
		i++;
	} while(mycustomttable[i].chipID);
}

static bool sisfb_interpret_edid(struct sisfb_monitor *monitor, u8 *buffer)
{
	int i, j, xres, yres, refresh, index;
	u32 emodes;

	if(buffer[0] != 0x00 || buffer[1] != 0xff ||
	   buffer[2] != 0xff || buffer[3] != 0xff ||
	   buffer[4] != 0xff || buffer[5] != 0xff ||
	   buffer[6] != 0xff || buffer[7] != 0x00) {
		printk(KERN_DEBUG "sisfb: Bad EDID header\n");
		return false;
	}

	if(buffer[0x12] != 0x01) {
		printk(KERN_INFO "sisfb: EDID version %d not supported\n",
			buffer[0x12]);
		return false;
	}

	monitor->feature = buffer[0x18];

	if(!(buffer[0x14] & 0x80)) {
		if(!(buffer[0x14] & 0x08)) {
			printk(KERN_INFO
				"sisfb: WARNING: Monitor does not support separate syncs\n");
		}
	}

	if(buffer[0x13] >= 0x01) {
	   /* EDID V1 rev 1 and 2: Search for monitor descriptor
	    * to extract ranges
	    */
	    j = 0x36;
	    for(i=0; i<4; i++) {
	       if(buffer[j]     == 0x00 && buffer[j + 1] == 0x00 &&
		  buffer[j + 2] == 0x00 && buffer[j + 3] == 0xfd &&
		  buffer[j + 4] == 0x00) {
		  monitor->hmin = buffer[j + 7];
		  monitor->hmax = buffer[j + 8];
		  monitor->vmin = buffer[j + 5];
		  monitor->vmax = buffer[j + 6];
		  monitor->dclockmax = buffer[j + 9] * 10 * 1000;
		  monitor->datavalid = true;
		  break;
	       }
	       j += 18;
	    }
	}

	if(!monitor->datavalid) {
	   /* Otherwise: Get a range from the list of supported
	    * Estabished Timings. This is not entirely accurate,
	    * because fixed frequency monitors are not supported
	    * that way.
	    */
	   monitor->hmin = 65535; monitor->hmax = 0;
	   monitor->vmin = 65535; monitor->vmax = 0;
	   monitor->dclockmax = 0;
	   emodes = buffer[0x23] | (buffer[0x24] << 8) | (buffer[0x25] << 16);
	   for(i = 0; i < 13; i++) {
	      if(emodes & sisfb_ddcsmodes[i].mask) {
		 if(monitor->hmin > sisfb_ddcsmodes[i].h) monitor->hmin = sisfb_ddcsmodes[i].h;
		 if(monitor->hmax < sisfb_ddcsmodes[i].h) monitor->hmax = sisfb_ddcsmodes[i].h + 1;
		 if(monitor->vmin > sisfb_ddcsmodes[i].v) monitor->vmin = sisfb_ddcsmodes[i].v;
		 if(monitor->vmax < sisfb_ddcsmodes[i].v) monitor->vmax = sisfb_ddcsmodes[i].v;
		 if(monitor->dclockmax < sisfb_ddcsmodes[i].d) monitor->dclockmax = sisfb_ddcsmodes[i].d;
	      }
	   }
	   index = 0x26;
	   for(i = 0; i < 8; i++) {
	      xres = (buffer[index] + 31) * 8;
	      switch(buffer[index + 1] & 0xc0) {
		 case 0xc0: yres = (xres * 9) / 16; break;
		 case 0x80: yres = (xres * 4) /  5; break;
		 case 0x40: yres = (xres * 3) /  4; break;
		 default:   yres = xres;	    break;
	      }
	      refresh = (buffer[index + 1] & 0x3f) + 60;
	      if((xres >= 640) && (yres >= 480)) {
		 for(j = 0; j < 8; j++) {
		    if((xres == sisfb_ddcfmodes[j].x) &&
		       (yres == sisfb_ddcfmodes[j].y) &&
		       (refresh == sisfb_ddcfmodes[j].v)) {
		      if(monitor->hmin > sisfb_ddcfmodes[j].h) monitor->hmin = sisfb_ddcfmodes[j].h;
		      if(monitor->hmax < sisfb_ddcfmodes[j].h) monitor->hmax = sisfb_ddcfmodes[j].h + 1;
		      if(monitor->vmin > sisfb_ddcsmodes[j].v) monitor->vmin = sisfb_ddcsmodes[j].v;
		      if(monitor->vmax < sisfb_ddcsmodes[j].v) monitor->vmax = sisfb_ddcsmodes[j].v;
		      if(monitor->dclockmax < sisfb_ddcsmodes[j].d) monitor->dclockmax = sisfb_ddcsmodes[j].d;
		    }
		 }
	      }
	      index += 2;
	   }
	   if((monitor->hmin <= monitor->hmax) && (monitor->vmin <= monitor->vmax)) {
	      monitor->datavalid = true;
	   }
	}

	return monitor->datavalid;
}

static void sisfb_handle_ddc(struct sis_video_info *ivideo,
			     struct sisfb_monitor *monitor, int crtno)
{
	unsigned short temp, i, realcrtno = crtno;
	unsigned char  buffer[256];

	monitor->datavalid = false;

	if(crtno) {
	   if(ivideo->vbflags & CRT2_LCD)      realcrtno = 1;
	   else if(ivideo->vbflags & CRT2_VGA) realcrtno = 2;
	   else return;
	}

	if((ivideo->sisfb_crt1off) && (!crtno))
		return;

	temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags, ivideo->sisvga_engine,
				realcrtno, 0, &buffer[0], ivideo->vbflags2);
	if((!temp) || (temp == 0xffff)) {
	   printk(KERN_INFO "sisfb: CRT%d DDC probing failed\n", crtno + 1);
	   return;
	} else {
	   printk(KERN_INFO "sisfb: CRT%d DDC supported\n", crtno + 1);
	   printk(KERN_INFO "sisfb: CRT%d DDC level: %s%s%s%s\n",
		crtno + 1,
		(temp & 0x1a) ? "" : "[none of the supported]",
		(temp & 0x02) ? "2 " : "",
		(temp & 0x08) ? "D&P" : "",
		(temp & 0x10) ? "FPDI-2" : "");
	   if(temp & 0x02) {
	      i = 3;  /* Number of retrys */
	      do {
		 temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags, ivideo->sisvga_engine,
				     realcrtno, 1, &buffer[0], ivideo->vbflags2);
	      } while((temp) && i--);
	      if(!temp) {
		 if(sisfb_interpret_edid(monitor, &buffer[0])) {
		    printk(KERN_INFO "sisfb: Monitor range H %d-%dKHz, V %d-%dHz, Max. dotclock %dMHz\n",
			monitor->hmin, monitor->hmax, monitor->vmin, monitor->vmax,
			monitor->dclockmax / 1000);
		 } else {
		    printk(KERN_INFO "sisfb: CRT%d DDC EDID corrupt\n", crtno + 1);
		 }
	      } else {
		 printk(KERN_INFO "sisfb: CRT%d DDC reading failed\n", crtno + 1);
	      }
	   } else {
	      printk(KERN_INFO "sisfb: VESA D&P and FPDI-2 not supported yet\n");
	   }
	}
}

/* -------------- Mode validation --------------- */

static bool
sisfb_verify_rate(struct sis_video_info *ivideo, struct sisfb_monitor *monitor,
		int mode_idx, int rate_idx, int rate)
{
	int htotal, vtotal;
	unsigned int dclock, hsync;

	if(!monitor->datavalid)
		return true;

	if(mode_idx < 0)
		return false;

	/* Skip for 320x200, 320x240, 640x400 */
	switch(sisbios_mode[mode_idx].mode_no[ivideo->mni]) {
	case 0x59:
	case 0x41:
	case 0x4f:
	case 0x50:
	case 0x56:
	case 0x53:
	case 0x2f:
	case 0x5d:
	case 0x5e:
		return true;
#ifdef CONFIG_FB_SIS_315
	case 0x5a:
	case 0x5b:
		if(ivideo->sisvga_engine == SIS_315_VGA) return true;
#endif
	}

	if(rate < (monitor->vmin - 1))
		return false;
	if(rate > (monitor->vmax + 1))
		return false;

	if(sisfb_gettotalfrommode(&ivideo->SiS_Pr,
				  sisbios_mode[mode_idx].mode_no[ivideo->mni],
				  &htotal, &vtotal, rate_idx)) {
		dclock = (htotal * vtotal * rate) / 1000;
		if(dclock > (monitor->dclockmax + 1000))
			return false;
		hsync = dclock / htotal;
		if(hsync < (monitor->hmin - 1))
			return false;
		if(hsync > (monitor->hmax + 1))
			return false;
        } else {
		return false;
	}
	return true;
}

static int
sisfb_validate_mode(struct sis_video_info *ivideo, int myindex, u32 vbflags)
{
	u16 xres=0, yres, myres;

#ifdef CONFIG_FB_SIS_300
	if (ivideo->sisvga_engine == SIS_300_VGA) {
		if (!(sisbios_mode[myindex].chipset & MD_SIS300))
			return -1 ;
	}
#endif
#ifdef CONFIG_FB_SIS_315
	if (ivideo->sisvga_engine == SIS_315_VGA) {
		if (!(sisbios_mode[myindex].chipset & MD_SIS315))
			return -1;
	}
#endif

	myres = sisbios_mode[myindex].yres;

	switch (vbflags & VB_DISPTYPE_DISP2) {

	case CRT2_LCD:
		xres = ivideo->lcdxres; yres = ivideo->lcdyres;

		if ((ivideo->SiS_Pr.SiS_CustomT != CUT_PANEL848) &&
		    (ivideo->SiS_Pr.SiS_CustomT != CUT_PANEL856)) {
			if (sisbios_mode[myindex].xres > xres)
				return -1;
			if (myres > yres)
				return -1;
		}

		if (ivideo->sisfb_fstn) {
			if (sisbios_mode[myindex].xres == 320) {
				if (myres == 240) {
					switch (sisbios_mode[myindex].mode_no[1]) {
						case 0x50: myindex = MODE_FSTN_8;  break;
						case 0x56: myindex = MODE_FSTN_16; break;
						case 0x53: return -1;
					}
				}
			}
		}

		if (SiS_GetModeID_LCD(ivideo->sisvga_engine, vbflags, sisbios_mode[myindex].xres,
			 	sisbios_mode[myindex].yres, 0, ivideo->sisfb_fstn,
			 	ivideo->SiS_Pr.SiS_CustomT, xres, yres, ivideo->vbflags2) < 0x14) {
			return -1;
		}
		break;

	case CRT2_TV:
		if (SiS_GetModeID_TV(ivideo->sisvga_engine, vbflags, sisbios_mode[myindex].xres,
				sisbios_mode[myindex].yres, 0, ivideo->vbflags2) < 0x14) {
			return -1;
		}
		break;

	case CRT2_VGA:
		if (SiS_GetModeID_VGA2(ivideo->sisvga_engine, vbflags, sisbios_mode[myindex].xres,
				sisbios_mode[myindex].yres, 0, ivideo->vbflags2) < 0x14) {
			return -1;
		}
		break;
	}

	return myindex;
}

static u8
sisfb_search_refresh_rate(struct sis_video_info *ivideo, unsigned int rate, int mode_idx)
{
	int i = 0;
	u16 xres = sisbios_mode[mode_idx].xres;
	u16 yres = sisbios_mode[mode_idx].yres;

	ivideo->rate_idx = 0;
	while((sisfb_vrate[i].idx != 0) && (sisfb_vrate[i].xres <= xres)) {
		if((sisfb_vrate[i].xres == xres) && (sisfb_vrate[i].yres == yres)) {
			if(sisfb_vrate[i].refresh == rate) {
				ivideo->rate_idx = sisfb_vrate[i].idx;
				break;
			} else if(sisfb_vrate[i].refresh > rate) {
				if((sisfb_vrate[i].refresh - rate) <= 3) {
					DPRINTK("sisfb: Adjusting rate from %d up to %d\n",
						rate, sisfb_vrate[i].refresh);
					ivideo->rate_idx = sisfb_vrate[i].idx;
					ivideo->refresh_rate = sisfb_vrate[i].refresh;
				} else if((sisfb_vrate[i].idx != 1) &&
						((rate - sisfb_vrate[i-1].refresh) <= 2)) {
					DPRINTK("sisfb: Adjusting rate from %d down to %d\n",
						rate, sisfb_vrate[i-1].refresh);
					ivideo->rate_idx = sisfb_vrate[i-1].idx;
					ivideo->refresh_rate = sisfb_vrate[i-1].refresh;
				}
				break;
			} else if((rate - sisfb_vrate[i].refresh) <= 2) {
				DPRINTK("sisfb: Adjusting rate from %d down to %d\n",
						rate, sisfb_vrate[i].refresh);
				ivideo->rate_idx = sisfb_vrate[i].idx;
				break;
			}
		}
		i++;
	}
	if(ivideo->rate_idx > 0) {
		return ivideo->rate_idx;
	} else {
		printk(KERN_INFO "sisfb: Unsupported rate %d for %dx%d\n",
				rate, xres, yres);
		return 0;
	}
}

static bool
sisfb_bridgeisslave(struct sis_video_info *ivideo)
{
	unsigned char P1_00;

	if(!(ivideo->vbflags2 & VB2_VIDEOBRIDGE))
		return false;

	P1_00 = SiS_GetReg(SISPART1, 0x00);
	if( ((ivideo->sisvga_engine == SIS_300_VGA) && (P1_00 & 0xa0) == 0x20) ||
	    ((ivideo->sisvga_engine == SIS_315_VGA) && (P1_00 & 0x50) == 0x10) ) {
		return true;
	} else {
		return false;
	}
}

static bool
sisfballowretracecrt1(struct sis_video_info *ivideo)
{
	u8 temp;

	temp = SiS_GetReg(SISCR, 0x17);
	if(!(temp & 0x80))
		return false;

	temp = SiS_GetReg(SISSR, 0x1f);
	if(temp & 0xc0)
		return false;

	return true;
}

static bool
sisfbcheckvretracecrt1(struct sis_video_info *ivideo)
{
	if(!sisfballowretracecrt1(ivideo))
		return false;

	if (SiS_GetRegByte(SISINPSTAT) & 0x08)
		return true;
	else
		return false;
}

static void
sisfbwaitretracecrt1(struct sis_video_info *ivideo)
{
	int watchdog;

	if(!sisfballowretracecrt1(ivideo))
		return;

	watchdog = 65536;
	while ((!(SiS_GetRegByte(SISINPSTAT) & 0x08)) && --watchdog);
	watchdog = 65536;
	while ((SiS_GetRegByte(SISINPSTAT) & 0x08) && --watchdog);
}

static bool
sisfbcheckvretracecrt2(struct sis_video_info *ivideo)
{
	unsigned char temp, reg;

	switch(ivideo->sisvga_engine) {
	case SIS_300_VGA: reg = 0x25; break;
	case SIS_315_VGA: reg = 0x30; break;
	default:	  return false;
	}

	temp = SiS_GetReg(SISPART1, reg);
	if(temp & 0x02)
		return true;
	else
		return false;
}

static bool
sisfb_CheckVBRetrace(struct sis_video_info *ivideo)
{
	if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
		if(!sisfb_bridgeisslave(ivideo)) {
			return sisfbcheckvretracecrt2(ivideo);
		}
	}
	return sisfbcheckvretracecrt1(ivideo);
}

static u32
sisfb_setupvbblankflags(struct sis_video_info *ivideo, u32 *vcount, u32 *hcount)
{
	u8 idx, reg1, reg2, reg3, reg4;
	u32 ret = 0;

	(*vcount) = (*hcount) = 0;

	if((ivideo->currentvbflags & VB_DISPTYPE_DISP2) && (!(sisfb_bridgeisslave(ivideo)))) {

		ret |= (FB_VBLANK_HAVE_VSYNC  |
			FB_VBLANK_HAVE_HBLANK |
			FB_VBLANK_HAVE_VBLANK |
			FB_VBLANK_HAVE_VCOUNT |
			FB_VBLANK_HAVE_HCOUNT);
		switch(ivideo->sisvga_engine) {
			case SIS_300_VGA: idx = 0x25; break;
			default:
			case SIS_315_VGA: idx = 0x30; break;
		}
		reg1 = SiS_GetReg(SISPART1, (idx+0)); /* 30 */
		reg2 = SiS_GetReg(SISPART1, (idx+1)); /* 31 */
		reg3 = SiS_GetReg(SISPART1, (idx+2)); /* 32 */
		reg4 = SiS_GetReg(SISPART1, (idx+3)); /* 33 */
		if(reg1 & 0x01) ret |= FB_VBLANK_VBLANKING;
		if(reg1 & 0x02) ret |= FB_VBLANK_VSYNCING;
		if(reg4 & 0x80) ret |= FB_VBLANK_HBLANKING;
		(*vcount) = reg3 | ((reg4 & 0x70) << 4);
		(*hcount) = reg2 | ((reg4 & 0x0f) << 8);

	} else if(sisfballowretracecrt1(ivideo)) {

		ret |= (FB_VBLANK_HAVE_VSYNC  |
			FB_VBLANK_HAVE_VBLANK |
			FB_VBLANK_HAVE_VCOUNT |
			FB_VBLANK_HAVE_HCOUNT);
		reg1 = SiS_GetRegByte(SISINPSTAT);
		if(reg1 & 0x08) ret |= FB_VBLANK_VSYNCING;
		if(reg1 & 0x01) ret |= FB_VBLANK_VBLANKING;
		reg1 = SiS_GetReg(SISCR, 0x20);
		reg1 = SiS_GetReg(SISCR, 0x1b);
		reg2 = SiS_GetReg(SISCR, 0x1c);
		reg3 = SiS_GetReg(SISCR, 0x1d);
		(*vcount) = reg2 | ((reg3 & 0x07) << 8);
		(*hcount) = (reg1 | ((reg3 & 0x10) << 4)) << 3;
	}

	return ret;
}

static int
sisfb_myblank(struct sis_video_info *ivideo, int blank)
{
	u8 sr01, sr11, sr1f, cr63=0, p2_0, p1_13;
	bool backlight = true;

	switch(blank) {
		case FB_BLANK_UNBLANK:	/* on */
			sr01  = 0x00;
			sr11  = 0x00;
			sr1f  = 0x00;
			cr63  = 0x00;
			p2_0  = 0x20;
			p1_13 = 0x00;
			backlight = true;
			break;
		case FB_BLANK_NORMAL:	/* blank */
			sr01  = 0x20;
			sr11  = 0x00;
			sr1f  = 0x00;
			cr63  = 0x00;
			p2_0  = 0x20;
			p1_13 = 0x00;
			backlight = true;
			break;
		case FB_BLANK_VSYNC_SUSPEND:	/* no vsync */
			sr01  = 0x20;
			sr11  = 0x08;
			sr1f  = 0x80;
			cr63  = 0x40;
			p2_0  = 0x40;
			p1_13 = 0x80;
			backlight = false;
			break;
		case FB_BLANK_HSYNC_SUSPEND:	/* no hsync */
			sr01  = 0x20;
			sr11  = 0x08;
			sr1f  = 0x40;
			cr63  = 0x40;
			p2_0  = 0x80;
			p1_13 = 0x40;
			backlight = false;
			break;
		case FB_BLANK_POWERDOWN:	/* off */
			sr01  = 0x20;
			sr11  = 0x08;
			sr1f  = 0xc0;
			cr63  = 0x40;
			p2_0  = 0xc0;
			p1_13 = 0xc0;
			backlight = false;
			break;
		default:
			return 1;
	}

	if(ivideo->currentvbflags & VB_DISPTYPE_CRT1) {

		if( (!ivideo->sisfb_thismonitor.datavalid) ||
		    ((ivideo->sisfb_thismonitor.datavalid) &&
		     (ivideo->sisfb_thismonitor.feature & 0xe0))) {

			if(ivideo->sisvga_engine == SIS_315_VGA) {
				SiS_SetRegANDOR(SISCR, ivideo->SiS_Pr.SiS_MyCR63, 0xbf, cr63);
			}

			if(!(sisfb_bridgeisslave(ivideo))) {
				SiS_SetRegANDOR(SISSR, 0x01, ~0x20, sr01);
				SiS_SetRegANDOR(SISSR, 0x1f, 0x3f, sr1f);
			}
		}

	}

	if(ivideo->currentvbflags & CRT2_LCD) {

		if(ivideo->vbflags2 & VB2_SISLVDSBRIDGE) {
			if(backlight) {
				SiS_SiS30xBLOn(&ivideo->SiS_Pr);
			} else {
				SiS_SiS30xBLOff(&ivideo->SiS_Pr);
			}
		} else if(ivideo->sisvga_engine == SIS_315_VGA) {
#ifdef CONFIG_FB_SIS_315
			if(ivideo->vbflags2 & VB2_CHRONTEL) {
				if(backlight) {
					SiS_Chrontel701xBLOn(&ivideo->SiS_Pr);
				} else {
					SiS_Chrontel701xBLOff(&ivideo->SiS_Pr);
				}
			}
#endif
		}

		if(((ivideo->sisvga_engine == SIS_300_VGA) &&
		    (ivideo->vbflags2 & (VB2_301|VB2_30xBDH|VB2_LVDS))) ||
		   ((ivideo->sisvga_engine == SIS_315_VGA) &&
		    ((ivideo->vbflags2 & (VB2_LVDS | VB2_CHRONTEL)) == VB2_LVDS))) {
			SiS_SetRegANDOR(SISSR, 0x11, ~0x0c, sr11);
		}

		if(ivideo->sisvga_engine == SIS_300_VGA) {
			if((ivideo->vbflags2 & VB2_30xB) &&
			   (!(ivideo->vbflags2 & VB2_30xBDH))) {
				SiS_SetRegANDOR(SISPART1, 0x13, 0x3f, p1_13);
			}
		} else if(ivideo->sisvga_engine == SIS_315_VGA) {
			if((ivideo->vbflags2 & VB2_30xB) &&
			   (!(ivideo->vbflags2 & VB2_30xBDH))) {
				SiS_SetRegANDOR(SISPART2, 0x00, 0x1f, p2_0);
			}
		}

	} else if(ivideo->currentvbflags & CRT2_VGA) {

		if(ivideo->vbflags2 & VB2_30xB) {
			SiS_SetRegANDOR(SISPART2, 0x00, 0x1f, p2_0);
		}

	}

	return 0;
}

/* ------------- Callbacks from init.c/init301.c  -------------- */

#ifdef CONFIG_FB_SIS_300
unsigned int
sisfb_read_nbridge_pci_dword(struct SiS_Private *SiS_Pr, int reg)
{
   struct sis_video_info *ivideo = (struct sis_video_info *)SiS_Pr->ivideo;
   u32 val = 0;

   pci_read_config_dword(ivideo->nbridge, reg, &val);
   return (unsigned int)val;
}

void
sisfb_write_nbridge_pci_dword(struct SiS_Private *SiS_Pr, int reg, unsigned int val)
{
   struct sis_video_info *ivideo = (struct sis_video_info *)SiS_Pr->ivideo;

   pci_write_config_dword(ivideo->nbridge, reg, (u32)val);
}

unsigned int
sisfb_read_lpc_pci_dword(struct SiS_Private *SiS_Pr, int reg)
{
   struct sis_video_info *ivideo = (struct sis_video_info *)SiS_Pr->ivideo;
   u32 val = 0;

   if(!ivideo->lpcdev) return 0;

   pci_read_config_dword(ivideo->lpcdev, reg, &val);
   return (unsigned int)val;
}
#endif

#ifdef CONFIG_FB_SIS_315
void
sisfb_write_nbridge_pci_byte(struct SiS_Private *SiS_Pr, int reg, unsigned char val)
{
   struct sis_video_info *ivideo = (struct sis_video_info *)SiS_Pr->ivideo;

   pci_write_config_byte(ivideo->nbridge, reg, (u8)val);
}

unsigned int
sisfb_read_mio_pci_word(struct SiS_Private *SiS_Pr, int reg)
{
   struct sis_video_info *ivideo = (struct sis_video_info *)SiS_Pr->ivideo;
   u16 val = 0;

   if(!ivideo->lpcdev) return 0;

   pci_read_config_word(ivideo->lpcdev, reg, &val);
   return (unsigned int)val;
}
#endif

/* ----------- FBDev related routines for all series ----------- */

static int
sisfb_get_cmap_len(const struct fb_var_screeninfo *var)
{
	return (var->bits_per_pixel == 8) ? 256 : 16;
}

static void
sisfb_set_vparms(struct sis_video_info *ivideo)
{
	switch(ivideo->video_bpp) {
	case 8:
		ivideo->DstColor = 0x0000;
		ivideo->SiS310_AccelDepth = 0x00000000;
		ivideo->video_cmap_len = 256;
		break;
	case 16:
		ivideo->DstColor = 0x8000;
		ivideo->SiS310_AccelDepth = 0x00010000;
		ivideo->video_cmap_len = 16;
		break;
	case 32:
		ivideo->DstColor = 0xC000;
		ivideo->SiS310_AccelDepth = 0x00020000;
		ivideo->video_cmap_len = 16;
		break;
	default:
		ivideo->video_cmap_len = 16;
		printk(KERN_ERR "sisfb: Unsupported depth %d", ivideo->video_bpp);
		ivideo->accel = 0;
	}
}

static int
sisfb_calc_maxyres(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	int maxyres = ivideo->sisfb_mem / (var->xres_virtual * (var->bits_per_pixel >> 3));

	if(maxyres > 32767) maxyres = 32767;

	return maxyres;
}

static void
sisfb_calc_pitch(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	ivideo->video_linelength = var->xres_virtual * (var->bits_per_pixel >> 3);
	ivideo->scrnpitchCRT1 = ivideo->video_linelength;
	if(!(ivideo->currentvbflags & CRT1_LCDA)) {
		if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
			ivideo->scrnpitchCRT1 <<= 1;
		}
	}
}

static void
sisfb_set_pitch(struct sis_video_info *ivideo)
{
	bool isslavemode = false;
	unsigned short HDisplay1 = ivideo->scrnpitchCRT1 >> 3;
	unsigned short HDisplay2 = ivideo->video_linelength >> 3;

	if(sisfb_bridgeisslave(ivideo)) isslavemode = true;

	/* We need to set pitch for CRT1 if bridge is in slave mode, too */
	if((ivideo->currentvbflags & VB_DISPTYPE_DISP1) || (isslavemode)) {
		SiS_SetReg(SISCR, 0x13, (HDisplay1 & 0xFF));
		SiS_SetRegANDOR(SISSR, 0x0E, 0xF0, (HDisplay1 >> 8));
	}

	/* We must not set the pitch for CRT2 if bridge is in slave mode */
	if((ivideo->currentvbflags & VB_DISPTYPE_DISP2) && (!isslavemode)) {
		SiS_SetRegOR(SISPART1, ivideo->CRT2_write_enable, 0x01);
		SiS_SetReg(SISPART1, 0x07, (HDisplay2 & 0xFF));
		SiS_SetRegANDOR(SISPART1, 0x09, 0xF0, (HDisplay2 >> 8));
	}
}

static void
sisfb_bpp_to_var(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	ivideo->video_cmap_len = sisfb_get_cmap_len(var);

	switch(var->bits_per_pixel) {
	case 8:
		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 8;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
}

static int
sisfb_set_mode(struct sis_video_info *ivideo, int clrscrn)
{
	unsigned short modeno = ivideo->mode_no;

	/* >=2.6.12's fbcon clears the screen anyway */
	modeno |= 0x80;

	SiS_SetReg(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

	sisfb_pre_setmode(ivideo);

	if(!SiSSetMode(&ivideo->SiS_Pr, modeno)) {
		printk(KERN_ERR "sisfb: Setting mode[0x%x] failed\n", ivideo->mode_no);
		return -EINVAL;
	}

	SiS_SetReg(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

	sisfb_post_setmode(ivideo);

	return 0;
}


static int
sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	unsigned int htotal = 0, vtotal = 0;
	unsigned int drate = 0, hrate = 0;
	int found_mode = 0, ret;
	int old_mode;
	u32 pixclock;

	htotal = var->left_margin + var->xres + var->right_margin + var->hsync_len;

	vtotal = var->upper_margin + var->lower_margin + var->vsync_len;

	pixclock = var->pixclock;

	if((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED) {
		vtotal += var->yres;
		vtotal <<= 1;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vtotal += var->yres;
		vtotal <<= 2;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vtotal += var->yres;
		vtotal <<= 1;
	} else 	vtotal += var->yres;

	if(!(htotal) || !(vtotal)) {
		DPRINTK("sisfb: Invalid 'var' information\n");
		return -EINVAL;
	}

	if(pixclock && htotal && vtotal) {
		drate = 1000000000 / pixclock;
		hrate = (drate * 1000) / htotal;
		ivideo->refresh_rate = (unsigned int) (hrate * 2 / vtotal);
	} else {
		ivideo->refresh_rate = 60;
	}

	old_mode = ivideo->sisfb_mode_idx;
	ivideo->sisfb_mode_idx = 0;

	while( (sisbios_mode[ivideo->sisfb_mode_idx].mode_no[0] != 0) &&
	       (sisbios_mode[ivideo->sisfb_mode_idx].xres <= var->xres) ) {
		if( (sisbios_mode[ivideo->sisfb_mode_idx].xres == var->xres) &&
		    (sisbios_mode[ivideo->sisfb_mode_idx].yres == var->yres) &&
		    (sisbios_mode[ivideo->sisfb_mode_idx].bpp == var->bits_per_pixel)) {
			ivideo->mode_no = sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni];
			found_mode = 1;
			break;
		}
		ivideo->sisfb_mode_idx++;
	}

	if(found_mode) {
		ivideo->sisfb_mode_idx = sisfb_validate_mode(ivideo,
				ivideo->sisfb_mode_idx, ivideo->currentvbflags);
	} else {
		ivideo->sisfb_mode_idx = -1;
	}

       	if(ivideo->sisfb_mode_idx < 0) {
		printk(KERN_ERR "sisfb: Mode %dx%dx%d not supported\n", var->xres,
		       var->yres, var->bits_per_pixel);
		ivideo->sisfb_mode_idx = old_mode;
		return -EINVAL;
	}

	ivideo->mode_no = sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni];

	if(sisfb_search_refresh_rate(ivideo, ivideo->refresh_rate, ivideo->sisfb_mode_idx) == 0) {
		ivideo->rate_idx = sisbios_mode[ivideo->sisfb_mode_idx].rate_idx;
		ivideo->refresh_rate = 60;
	}

	if(isactive) {
		/* If acceleration to be used? Need to know
		 * before pre/post_set_mode()
		 */
		ivideo->accel = 0;
#if defined(FBINFO_HWACCEL_DISABLED) && defined(FBINFO_HWACCEL_XPAN)
#ifdef STUPID_ACCELF_TEXT_SHIT
		if(var->accel_flags & FB_ACCELF_TEXT) {
			info->flags &= ~FBINFO_HWACCEL_DISABLED;
		} else {
			info->flags |= FBINFO_HWACCEL_DISABLED;
		}
#endif
		if(!(info->flags & FBINFO_HWACCEL_DISABLED)) ivideo->accel = -1;
#else
		if(var->accel_flags & FB_ACCELF_TEXT) ivideo->accel = -1;
#endif

		if((ret = sisfb_set_mode(ivideo, 1))) {
			return ret;
		}

		ivideo->video_bpp    = sisbios_mode[ivideo->sisfb_mode_idx].bpp;
		ivideo->video_width  = sisbios_mode[ivideo->sisfb_mode_idx].xres;
		ivideo->video_height = sisbios_mode[ivideo->sisfb_mode_idx].yres;

		sisfb_calc_pitch(ivideo, var);
		sisfb_set_pitch(ivideo);

		sisfb_set_vparms(ivideo);

		ivideo->current_width = ivideo->video_width;
		ivideo->current_height = ivideo->video_height;
		ivideo->current_bpp = ivideo->video_bpp;
		ivideo->current_htotal = htotal;
		ivideo->current_vtotal = vtotal;
		ivideo->current_linelength = ivideo->video_linelength;
		ivideo->current_pixclock = var->pixclock;
		ivideo->current_refresh_rate = ivideo->refresh_rate;
		ivideo->sisfb_lastrates[ivideo->mode_no] = ivideo->refresh_rate;
	}

	return 0;
}

static void
sisfb_set_base_CRT1(struct sis_video_info *ivideo, unsigned int base)
{
	SiS_SetReg(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

	SiS_SetReg(SISCR, 0x0D, base & 0xFF);
	SiS_SetReg(SISCR, 0x0C, (base >> 8) & 0xFF);
	SiS_SetReg(SISSR, 0x0D, (base >> 16) & 0xFF);
	if(ivideo->sisvga_engine == SIS_315_VGA) {
		SiS_SetRegANDOR(SISSR, 0x37, 0xFE, (base >> 24) & 0x01);
	}
}

static void
sisfb_set_base_CRT2(struct sis_video_info *ivideo, unsigned int base)
{
	if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
		SiS_SetRegOR(SISPART1, ivideo->CRT2_write_enable, 0x01);
		SiS_SetReg(SISPART1, 0x06, (base & 0xFF));
		SiS_SetReg(SISPART1, 0x05, ((base >> 8) & 0xFF));
		SiS_SetReg(SISPART1, 0x04, ((base >> 16) & 0xFF));
		if(ivideo->sisvga_engine == SIS_315_VGA) {
			SiS_SetRegANDOR(SISPART1, 0x02, 0x7F, ((base >> 24) & 0x01) << 7);
		}
	}
}

static int
sisfb_pan_var(struct sis_video_info *ivideo, struct fb_info *info,
	      struct fb_var_screeninfo *var)
{
	ivideo->current_base = var->yoffset * info->var.xres_virtual
			     + var->xoffset;

	/* calculate base bpp dep. */
	switch (info->var.bits_per_pixel) {
	case 32:
		break;
	case 16:
		ivideo->current_base >>= 1;
		break;
	case 8:
	default:
		ivideo->current_base >>= 2;
		break;
	}

	ivideo->current_base += (ivideo->video_offset >> 2);

	sisfb_set_base_CRT1(ivideo, ivideo->current_base);
	sisfb_set_base_CRT2(ivideo, ivideo->current_base);

	return 0;
}

static int
sisfb_open(struct fb_info *info, int user)
{
	return 0;
}

static int
sisfb_release(struct fb_info *info, int user)
{
	return 0;
}

static int
sisfb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
		unsigned transp, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	if(regno >= sisfb_get_cmap_len(&info->var))
		return 1;

	switch(info->var.bits_per_pixel) {
	case 8:
		SiS_SetRegByte(SISDACA, regno);
		SiS_SetRegByte(SISDACD, (red >> 10));
		SiS_SetRegByte(SISDACD, (green >> 10));
		SiS_SetRegByte(SISDACD, (blue >> 10));
		if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
			SiS_SetRegByte(SISDAC2A, regno);
			SiS_SetRegByte(SISDAC2D, (red >> 8));
			SiS_SetRegByte(SISDAC2D, (green >> 8));
			SiS_SetRegByte(SISDAC2D, (blue >> 8));
		}
		break;
	case 16:
		if (regno >= 16)
			break;

		((u32 *)(info->pseudo_palette))[regno] =
				(red & 0xf800)          |
				((green & 0xfc00) >> 5) |
				((blue & 0xf800) >> 11);
		break;
	case 32:
		if (regno >= 16)
			break;

		red >>= 8;
		green >>= 8;
		blue >>= 8;
		((u32 *)(info->pseudo_palette))[regno] =
				(red << 16) | (green << 8) | (blue);
		break;
	}
	return 0;
}

static int
sisfb_set_par(struct fb_info *info)
{
	int err;

	if((err = sisfb_do_set_var(&info->var, 1, info)))
		return err;

	sisfb_get_fix(&info->fix, -1, info);

	return 0;
}

static int
sisfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	unsigned int htotal = 0, vtotal = 0, myrateindex = 0;
	unsigned int drate = 0, hrate = 0, maxyres;
	int found_mode = 0;
	int refresh_rate, search_idx, tidx;
	bool recalc_clock = false;
	u32 pixclock;

	htotal = var->left_margin + var->xres + var->right_margin + var->hsync_len;

	vtotal = var->upper_margin + var->lower_margin + var->vsync_len;

	if (!var->pixclock)
		return -EINVAL;
	pixclock = var->pixclock;

	if((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED) {
		vtotal += var->yres;
		vtotal <<= 1;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vtotal += var->yres;
		vtotal <<= 2;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vtotal += var->yres;
		vtotal <<= 1;
	} else
		vtotal += var->yres;

	if(!(htotal) || !(vtotal)) {
		SISFAIL("sisfb: no valid timing data");
	}

	search_idx = 0;
	while( (sisbios_mode[search_idx].mode_no[0] != 0) &&
	       (sisbios_mode[search_idx].xres <= var->xres) ) {
		if( (sisbios_mode[search_idx].xres == var->xres) &&
		    (sisbios_mode[search_idx].yres == var->yres) &&
		    (sisbios_mode[search_idx].bpp == var->bits_per_pixel)) {
			if((tidx = sisfb_validate_mode(ivideo, search_idx,
						ivideo->currentvbflags)) > 0) {
				found_mode = 1;
				search_idx = tidx;
				break;
			}
		}
		search_idx++;
	}

	if(!found_mode) {
		search_idx = 0;
		while(sisbios_mode[search_idx].mode_no[0] != 0) {
		   if( (var->xres <= sisbios_mode[search_idx].xres) &&
		       (var->yres <= sisbios_mode[search_idx].yres) &&
		       (var->bits_per_pixel == sisbios_mode[search_idx].bpp) ) {
			if((tidx = sisfb_validate_mode(ivideo,search_idx,
						ivideo->currentvbflags)) > 0) {
				found_mode = 1;
				search_idx = tidx;
				break;
			}
		   }
		   search_idx++;
		}
		if(found_mode) {
			printk(KERN_DEBUG
				"sisfb: Adapted from %dx%dx%d to %dx%dx%d\n",
				var->xres, var->yres, var->bits_per_pixel,
				sisbios_mode[search_idx].xres,
				sisbios_mode[search_idx].yres,
				var->bits_per_pixel);
			var->xres = sisbios_mode[search_idx].xres;
			var->yres = sisbios_mode[search_idx].yres;
		} else {
			printk(KERN_ERR
				"sisfb: Failed to find supported mode near %dx%dx%d\n",
				var->xres, var->yres, var->bits_per_pixel);
			return -EINVAL;
		}
	}

	if( ((ivideo->vbflags2 & VB2_LVDS) ||
	     ((ivideo->vbflags2 & VB2_30xBDH) && (ivideo->currentvbflags & CRT2_LCD))) &&
	    (var->bits_per_pixel == 8) ) {
		/* Slave modes on LVDS and 301B-DH */
		refresh_rate = 60;
		recalc_clock = true;
	} else if( (ivideo->current_htotal == htotal) &&
		   (ivideo->current_vtotal == vtotal) &&
		   (ivideo->current_pixclock == pixclock) ) {
		/* x=x & y=y & c=c -> assume depth change */
		drate = 1000000000 / pixclock;
		hrate = (drate * 1000) / htotal;
		refresh_rate = (unsigned int) (hrate * 2 / vtotal);
	} else if( ( (ivideo->current_htotal != htotal) ||
		     (ivideo->current_vtotal != vtotal) ) &&
		   (ivideo->current_pixclock == var->pixclock) ) {
		/* x!=x | y!=y & c=c -> invalid pixclock */
		if(ivideo->sisfb_lastrates[sisbios_mode[search_idx].mode_no[ivideo->mni]]) {
			refresh_rate =
				ivideo->sisfb_lastrates[sisbios_mode[search_idx].mode_no[ivideo->mni]];
		} else if(ivideo->sisfb_parm_rate != -1) {
			/* Sic, sisfb_parm_rate - want to know originally desired rate here */
			refresh_rate = ivideo->sisfb_parm_rate;
		} else {
			refresh_rate = 60;
		}
		recalc_clock = true;
	} else if((pixclock) && (htotal) && (vtotal)) {
		drate = 1000000000 / pixclock;
		hrate = (drate * 1000) / htotal;
		refresh_rate = (unsigned int) (hrate * 2 / vtotal);
	} else if(ivideo->current_refresh_rate) {
		refresh_rate = ivideo->current_refresh_rate;
		recalc_clock = true;
	} else {
		refresh_rate = 60;
		recalc_clock = true;
	}

	myrateindex = sisfb_search_refresh_rate(ivideo, refresh_rate, search_idx);

	/* Eventually recalculate timing and clock */
	if(recalc_clock) {
		if(!myrateindex) myrateindex = sisbios_mode[search_idx].rate_idx;
		var->pixclock = (u32) (1000000000 / sisfb_mode_rate_to_dclock(&ivideo->SiS_Pr,
						sisbios_mode[search_idx].mode_no[ivideo->mni],
						myrateindex));
		sisfb_mode_rate_to_ddata(&ivideo->SiS_Pr,
					sisbios_mode[search_idx].mode_no[ivideo->mni],
					myrateindex, var);
		if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
			var->pixclock <<= 1;
		}
	}

	if(ivideo->sisfb_thismonitor.datavalid) {
		if(!sisfb_verify_rate(ivideo, &ivideo->sisfb_thismonitor, search_idx,
				myrateindex, refresh_rate)) {
			printk(KERN_INFO
				"sisfb: WARNING: Refresh rate exceeds monitor specs!\n");
		}
	}

	/* Adapt RGB settings */
	sisfb_bpp_to_var(ivideo, var);

	if(var->xres > var->xres_virtual)
		var->xres_virtual = var->xres;

	if(ivideo->sisfb_ypan) {
		maxyres = sisfb_calc_maxyres(ivideo, var);
		if(ivideo->sisfb_max) {
			var->yres_virtual = maxyres;
		} else {
			if(var->yres_virtual > maxyres) {
				var->yres_virtual = maxyres;
			}
		}
		if(var->yres_virtual <= var->yres) {
			var->yres_virtual = var->yres;
		}
	} else {
		if(var->yres != var->yres_virtual) {
			var->yres_virtual = var->yres;
		}
		var->xoffset = 0;
		var->yoffset = 0;
	}

	/* Truncate offsets to maximum if too high */
	if(var->xoffset > var->xres_virtual - var->xres) {
		var->xoffset = var->xres_virtual - var->xres - 1;
	}

	if(var->yoffset > var->yres_virtual - var->yres) {
		var->yoffset = var->yres_virtual - var->yres - 1;
	}

	/* Set everything else to 0 */
	var->red.msb_right =
		var->green.msb_right =
		var->blue.msb_right =
		var->transp.offset =
		var->transp.length =
		var->transp.msb_right = 0;

	return 0;
}

static int
sisfb_pan_display(struct fb_var_screeninfo *var, struct fb_info* info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	int err;

	if (var->vmode & FB_VMODE_YWRAP)
		return -EINVAL;

	if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	    var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;

	err = sisfb_pan_var(ivideo, info, var);
	if (err < 0)
		return err;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	return 0;
}

static int
sisfb_blank(int blank, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	return sisfb_myblank(ivideo, blank);
}

/* ----------- FBDev related routines for all series ---------- */

static int	sisfb_ioctl(struct fb_info *info, unsigned int cmd,
			    unsigned long arg)
{
	struct sis_video_info	*ivideo = (struct sis_video_info *)info->par;
	struct sis_memreq	sismemreq;
	struct fb_vblank	sisvbblank;
	u32			gpu32 = 0;
#ifndef __user
#define __user
#endif
	u32 __user 		*argp = (u32 __user *)arg;

	switch(cmd) {
	   case FBIO_ALLOC:
		if(!capable(CAP_SYS_RAWIO))
			return -EPERM;

		if(copy_from_user(&sismemreq, (void __user *)arg, sizeof(sismemreq)))
			return -EFAULT;

		sis_malloc(&sismemreq);

		if(copy_to_user((void __user *)arg, &sismemreq, sizeof(sismemreq))) {
			sis_free((u32)sismemreq.offset);
			return -EFAULT;
		}
		break;

	   case FBIO_FREE:
		if(!capable(CAP_SYS_RAWIO))
			return -EPERM;

		if(get_user(gpu32, argp))
			return -EFAULT;

		sis_free(gpu32);
		break;

	   case FBIOGET_VBLANK:

		memset(&sisvbblank, 0, sizeof(struct fb_vblank));

		sisvbblank.count = 0;
		sisvbblank.flags = sisfb_setupvbblankflags(ivideo, &sisvbblank.vcount, &sisvbblank.hcount);

		if(copy_to_user((void __user *)arg, &sisvbblank, sizeof(sisvbblank)))
			return -EFAULT;

		break;

	   case SISFB_GET_INFO_SIZE:
		return put_user(sizeof(struct sisfb_info), argp);

	   case SISFB_GET_INFO_OLD:
		if(ivideo->warncount++ < 10)
			printk(KERN_INFO
				"sisfb: Deprecated ioctl call received - update your application!\n");
		fallthrough;
	   case SISFB_GET_INFO:  /* For communication with X driver */
		ivideo->sisfb_infoblock.sisfb_id         = SISFB_ID;
		ivideo->sisfb_infoblock.sisfb_version    = VER_MAJOR;
		ivideo->sisfb_infoblock.sisfb_revision   = VER_MINOR;
		ivideo->sisfb_infoblock.sisfb_patchlevel = VER_LEVEL;
		ivideo->sisfb_infoblock.chip_id = ivideo->chip_id;
		ivideo->sisfb_infoblock.sisfb_pci_vendor = ivideo->chip_vendor;
		ivideo->sisfb_infoblock.memory = ivideo->video_size / 1024;
		ivideo->sisfb_infoblock.heapstart = ivideo->heapstart / 1024;
		if(ivideo->modechanged) {
			ivideo->sisfb_infoblock.fbvidmode = ivideo->mode_no;
		} else {
			ivideo->sisfb_infoblock.fbvidmode = ivideo->modeprechange;
		}
		ivideo->sisfb_infoblock.sisfb_caps = ivideo->caps;
		ivideo->sisfb_infoblock.sisfb_tqlen = ivideo->cmdQueueSize / 1024;
		ivideo->sisfb_infoblock.sisfb_pcibus = ivideo->pcibus;
		ivideo->sisfb_infoblock.sisfb_pcislot = ivideo->pcislot;
		ivideo->sisfb_infoblock.sisfb_pcifunc = ivideo->pcifunc;
		ivideo->sisfb_infoblock.sisfb_lcdpdc = ivideo->detectedpdc;
		ivideo->sisfb_infoblock.sisfb_lcdpdca = ivideo->detectedpdca;
		ivideo->sisfb_infoblock.sisfb_lcda = ivideo->detectedlcda;
		ivideo->sisfb_infoblock.sisfb_vbflags = ivideo->vbflags;
		ivideo->sisfb_infoblock.sisfb_currentvbflags = ivideo->currentvbflags;
		ivideo->sisfb_infoblock.sisfb_scalelcd = ivideo->SiS_Pr.UsePanelScaler;
		ivideo->sisfb_infoblock.sisfb_specialtiming = ivideo->SiS_Pr.SiS_CustomT;
		ivideo->sisfb_infoblock.sisfb_haveemi = ivideo->SiS_Pr.HaveEMI ? 1 : 0;
		ivideo->sisfb_infoblock.sisfb_haveemilcd = ivideo->SiS_Pr.HaveEMILCD ? 1 : 0;
		ivideo->sisfb_infoblock.sisfb_emi30 = ivideo->SiS_Pr.EMI_30;
		ivideo->sisfb_infoblock.sisfb_emi31 = ivideo->SiS_Pr.EMI_31;
		ivideo->sisfb_infoblock.sisfb_emi32 = ivideo->SiS_Pr.EMI_32;
		ivideo->sisfb_infoblock.sisfb_emi33 = ivideo->SiS_Pr.EMI_33;
		ivideo->sisfb_infoblock.sisfb_tvxpos = (u16)(ivideo->tvxpos + 32);
		ivideo->sisfb_infoblock.sisfb_tvypos = (u16)(ivideo->tvypos + 32);
		ivideo->sisfb_infoblock.sisfb_heapsize = ivideo->sisfb_heap_size / 1024;
		ivideo->sisfb_infoblock.sisfb_videooffset = ivideo->video_offset;
		ivideo->sisfb_infoblock.sisfb_curfstn = ivideo->curFSTN;
		ivideo->sisfb_infoblock.sisfb_curdstn = ivideo->curDSTN;
		ivideo->sisfb_infoblock.sisfb_vbflags2 = ivideo->vbflags2;
		ivideo->sisfb_infoblock.sisfb_can_post = ivideo->sisfb_can_post ? 1 : 0;
		ivideo->sisfb_infoblock.sisfb_card_posted = ivideo->sisfb_card_posted ? 1 : 0;
		ivideo->sisfb_infoblock.sisfb_was_boot_device = ivideo->sisfb_was_boot_device ? 1 : 0;

		if(copy_to_user((void __user *)arg, &ivideo->sisfb_infoblock,
						sizeof(ivideo->sisfb_infoblock)))
			return -EFAULT;

	        break;

	   case SISFB_GET_VBRSTATUS_OLD:
		if(ivideo->warncount++ < 10)
			printk(KERN_INFO
				"sisfb: Deprecated ioctl call received - update your application!\n");
		fallthrough;
	   case SISFB_GET_VBRSTATUS:
		if(sisfb_CheckVBRetrace(ivideo))
			return put_user((u32)1, argp);
		else
			return put_user((u32)0, argp);

	   case SISFB_GET_AUTOMAXIMIZE_OLD:
		if(ivideo->warncount++ < 10)
			printk(KERN_INFO
				"sisfb: Deprecated ioctl call received - update your application!\n");
		fallthrough;
	   case SISFB_GET_AUTOMAXIMIZE:
		if(ivideo->sisfb_max)
			return put_user((u32)1, argp);
		else
			return put_user((u32)0, argp);

	   case SISFB_SET_AUTOMAXIMIZE_OLD:
		if(ivideo->warncount++ < 10)
			printk(KERN_INFO
				"sisfb: Deprecated ioctl call received - update your application!\n");
		fallthrough;
	   case SISFB_SET_AUTOMAXIMIZE:
		if(get_user(gpu32, argp))
			return -EFAULT;

		ivideo->sisfb_max = (gpu32) ? 1 : 0;
		break;

	   case SISFB_SET_TVPOSOFFSET:
		if(get_user(gpu32, argp))
			return -EFAULT;

		sisfb_set_TVxposoffset(ivideo, ((int)(gpu32 >> 16)) - 32);
		sisfb_set_TVyposoffset(ivideo, ((int)(gpu32 & 0xffff)) - 32);
		break;

	   case SISFB_GET_TVPOSOFFSET:
		return put_user((u32)(((ivideo->tvxpos+32)<<16)|((ivideo->tvypos+32)&0xffff)),
							argp);

	   case SISFB_COMMAND:
		if(copy_from_user(&ivideo->sisfb_command, (void __user *)arg,
							sizeof(struct sisfb_cmd)))
			return -EFAULT;

		sisfb_handle_command(ivideo, &ivideo->sisfb_command);

		if(copy_to_user((void __user *)arg, &ivideo->sisfb_command,
							sizeof(struct sisfb_cmd)))
			return -EFAULT;

		break;

	   case SISFB_SET_LOCK:
		if(get_user(gpu32, argp))
			return -EFAULT;

		ivideo->sisfblocked = (gpu32) ? 1 : 0;
		break;

	   default:
#ifdef SIS_NEW_CONFIG_COMPAT
		return -ENOIOCTLCMD;
#else
		return -EINVAL;
#endif
	}
	return 0;
}

static int
sisfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

	strscpy(fix->id, ivideo->myid, sizeof(fix->id));

	mutex_lock(&info->mm_lock);
	fix->smem_start  = ivideo->video_base + ivideo->video_offset;
	fix->smem_len    = ivideo->sisfb_mem;
	mutex_unlock(&info->mm_lock);
	fix->type        = FB_TYPE_PACKED_PIXELS;
	fix->type_aux    = 0;
	fix->visual      = (ivideo->video_bpp == 8) ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	fix->xpanstep    = 1;
	fix->ypanstep 	 = (ivideo->sisfb_ypan) ? 1 : 0;
	fix->ywrapstep   = 0;
	fix->line_length = ivideo->video_linelength;
	fix->mmio_start  = ivideo->mmio_base;
	fix->mmio_len    = ivideo->mmio_size;
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		fix->accel = FB_ACCEL_SIS_GLAMOUR;
	} else if((ivideo->chip == SIS_330) ||
		  (ivideo->chip == SIS_760) ||
		  (ivideo->chip == SIS_761)) {
		fix->accel = FB_ACCEL_SIS_XABRE;
	} else if(ivideo->chip == XGI_20) {
		fix->accel = FB_ACCEL_XGI_VOLARI_Z;
	} else if(ivideo->chip >= XGI_40) {
		fix->accel = FB_ACCEL_XGI_VOLARI_V;
	} else {
		fix->accel = FB_ACCEL_SIS_GLAMOUR_2;
	}

	return 0;
}

/* ----------------  fb_ops structures ----------------- */

static const struct fb_ops sisfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= sisfb_open,
	.fb_release	= sisfb_release,
	.fb_check_var	= sisfb_check_var,
	.fb_set_par	= sisfb_set_par,
	.fb_setcolreg	= sisfb_setcolreg,
	.fb_pan_display	= sisfb_pan_display,
	.fb_blank	= sisfb_blank,
	.fb_fillrect	= fbcon_sis_fillrect,
	.fb_copyarea	= fbcon_sis_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_sync	= fbcon_sis_sync,
#ifdef SIS_NEW_CONFIG_COMPAT
	.fb_compat_ioctl= sisfb_ioctl,
#endif
	.fb_ioctl	= sisfb_ioctl
};

/* ---------------- Chip generation dependent routines ---------------- */

static struct pci_dev *sisfb_get_northbridge(int basechipid)
{
	struct pci_dev *pdev = NULL;
	int nbridgenum, nbridgeidx, i;
	static const unsigned short nbridgeids[] = {
		PCI_DEVICE_ID_SI_540,	/* for SiS 540 VGA */
		PCI_DEVICE_ID_SI_630,	/* for SiS 630/730 VGA */
		PCI_DEVICE_ID_SI_730,
		PCI_DEVICE_ID_SI_550,   /* for SiS 550 VGA */
		PCI_DEVICE_ID_SI_650,   /* for SiS 650/651/740 VGA */
		PCI_DEVICE_ID_SI_651,
		PCI_DEVICE_ID_SI_740,
		PCI_DEVICE_ID_SI_661,	/* for SiS 661/741/660/760/761 VGA */
		PCI_DEVICE_ID_SI_741,
		PCI_DEVICE_ID_SI_660,
		PCI_DEVICE_ID_SI_760,
		PCI_DEVICE_ID_SI_761
	};

	switch(basechipid) {
#ifdef CONFIG_FB_SIS_300
	case SIS_540:	nbridgeidx = 0; nbridgenum = 1; break;
	case SIS_630:	nbridgeidx = 1; nbridgenum = 2; break;
#endif
#ifdef CONFIG_FB_SIS_315
	case SIS_550:   nbridgeidx = 3; nbridgenum = 1; break;
	case SIS_650:	nbridgeidx = 4; nbridgenum = 3; break;
	case SIS_660:	nbridgeidx = 7; nbridgenum = 5; break;
#endif
	default:	return NULL;
	}
	for(i = 0; i < nbridgenum; i++) {
		if((pdev = pci_get_device(PCI_VENDOR_ID_SI,
				nbridgeids[nbridgeidx+i], NULL)))
			break;
	}
	return pdev;
}

static int sisfb_get_dram_size(struct sis_video_info *ivideo)
{
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
	u8 reg;
#endif

	ivideo->video_size = 0;
	ivideo->UMAsize = ivideo->LFBsize = 0;

	switch(ivideo->chip) {
#ifdef CONFIG_FB_SIS_300
	case SIS_300:
		reg = SiS_GetReg(SISSR, 0x14);
		ivideo->video_size = ((reg & 0x3F) + 1) << 20;
		break;
	case SIS_540:
	case SIS_630:
	case SIS_730:
		if(!ivideo->nbridge)
			return -1;
		pci_read_config_byte(ivideo->nbridge, 0x63, &reg);
		ivideo->video_size = 1 << (((reg & 0x70) >> 4) + 21);
		break;
#endif
#ifdef CONFIG_FB_SIS_315
	case SIS_315H:
	case SIS_315PRO:
	case SIS_315:
		reg = SiS_GetReg(SISSR, 0x14);
		ivideo->video_size = (1 << ((reg & 0xf0) >> 4)) << 20;
		switch((reg >> 2) & 0x03) {
		case 0x01:
		case 0x03:
			ivideo->video_size <<= 1;
			break;
		case 0x02:
			ivideo->video_size += (ivideo->video_size/2);
		}
		break;
	case SIS_330:
		reg = SiS_GetReg(SISSR, 0x14);
		ivideo->video_size = (1 << ((reg & 0xf0) >> 4)) << 20;
		if(reg & 0x0c) ivideo->video_size <<= 1;
		break;
	case SIS_550:
	case SIS_650:
	case SIS_740:
		reg = SiS_GetReg(SISSR, 0x14);
		ivideo->video_size = (((reg & 0x3f) + 1) << 2) << 20;
		break;
	case SIS_661:
	case SIS_741:
		reg = SiS_GetReg(SISCR, 0x79);
		ivideo->video_size = (1 << ((reg & 0xf0) >> 4)) << 20;
		break;
	case SIS_660:
	case SIS_760:
	case SIS_761:
		reg = SiS_GetReg(SISCR, 0x79);
		reg = (reg & 0xf0) >> 4;
		if(reg)	{
			ivideo->video_size = (1 << reg) << 20;
			ivideo->UMAsize = ivideo->video_size;
		}
		reg = SiS_GetReg(SISCR, 0x78);
		reg &= 0x30;
		if(reg) {
			if(reg == 0x10) {
				ivideo->LFBsize = (32 << 20);
			} else {
				ivideo->LFBsize = (64 << 20);
			}
			ivideo->video_size += ivideo->LFBsize;
		}
		break;
	case SIS_340:
	case XGI_20:
	case XGI_40:
		reg = SiS_GetReg(SISSR, 0x14);
		ivideo->video_size = (1 << ((reg & 0xf0) >> 4)) << 20;
		if(ivideo->chip != XGI_20) {
			reg = (reg & 0x0c) >> 2;
			if(ivideo->revision_id == 2) {
				if(reg & 0x01) reg = 0x02;
				else	       reg = 0x00;
			}
			if(reg == 0x02)		ivideo->video_size <<= 1;
			else if(reg == 0x03)	ivideo->video_size <<= 2;
		}
		break;
#endif
	default:
		return -1;
	}
	return 0;
}

/* -------------- video bridge device detection --------------- */

static void sisfb_detect_VB_connect(struct sis_video_info *ivideo)
{
	u8 cr32, temp;

	/* No CRT2 on XGI Z7 */
	if(ivideo->chip == XGI_20) {
		ivideo->sisfb_crt1off = 0;
		return;
	}

#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		temp = SiS_GetReg(SISSR, 0x17);
		if((temp & 0x0F) && (ivideo->chip != SIS_300)) {
			/* PAL/NTSC is stored on SR16 on such machines */
			if(!(ivideo->vbflags & (TV_PAL | TV_NTSC | TV_PALM | TV_PALN))) {
				temp = SiS_GetReg(SISSR, 0x16);
				if(temp & 0x20)
					ivideo->vbflags |= TV_PAL;
				else
					ivideo->vbflags |= TV_NTSC;
			}
		}
	}
#endif

	cr32 = SiS_GetReg(SISCR, 0x32);

	if(cr32 & SIS_CRT1) {
		ivideo->sisfb_crt1off = 0;
	} else {
		ivideo->sisfb_crt1off = (cr32 & 0xDF) ? 1 : 0;
	}

	ivideo->vbflags &= ~(CRT2_TV | CRT2_LCD | CRT2_VGA);

	if(cr32 & SIS_VB_TV)   ivideo->vbflags |= CRT2_TV;
	if(cr32 & SIS_VB_LCD)  ivideo->vbflags |= CRT2_LCD;
	if(cr32 & SIS_VB_CRT2) ivideo->vbflags |= CRT2_VGA;

	/* Check given parms for hardware compatibility.
	 * (Cannot do this in the search_xx routines since we don't
	 * know what hardware we are running on then)
	 */

	if(ivideo->chip != SIS_550) {
	   ivideo->sisfb_dstn = ivideo->sisfb_fstn = 0;
	}

	if(ivideo->sisfb_tvplug != -1) {
	   if( (ivideo->sisvga_engine != SIS_315_VGA) ||
	       (!(ivideo->vbflags2 & VB2_SISYPBPRBRIDGE)) ) {
	      if(ivideo->sisfb_tvplug & TV_YPBPR) {
		 ivideo->sisfb_tvplug = -1;
		 printk(KERN_ERR "sisfb: YPbPr not supported\n");
	      }
	   }
	}
	if(ivideo->sisfb_tvplug != -1) {
	   if( (ivideo->sisvga_engine != SIS_315_VGA) ||
	       (!(ivideo->vbflags2 & VB2_SISHIVISIONBRIDGE)) ) {
	      if(ivideo->sisfb_tvplug & TV_HIVISION) {
		 ivideo->sisfb_tvplug = -1;
		 printk(KERN_ERR "sisfb: HiVision not supported\n");
	      }
	   }
	}
	if(ivideo->sisfb_tvstd != -1) {
	   if( (!(ivideo->vbflags2 & VB2_SISBRIDGE)) &&
	       (!((ivideo->sisvga_engine == SIS_315_VGA) &&
			(ivideo->vbflags2 & VB2_CHRONTEL))) ) {
	      if(ivideo->sisfb_tvstd & (TV_PALM | TV_PALN | TV_NTSCJ)) {
		 ivideo->sisfb_tvstd = -1;
		 printk(KERN_ERR "sisfb: PALM/PALN/NTSCJ not supported\n");
	      }
	   }
	}

	/* Detect/set TV plug & type */
	if(ivideo->sisfb_tvplug != -1) {
		ivideo->vbflags |= ivideo->sisfb_tvplug;
	} else {
		if(cr32 & SIS_VB_YPBPR)     	 ivideo->vbflags |= (TV_YPBPR|TV_YPBPR525I); /* default: 480i */
		else if(cr32 & SIS_VB_HIVISION)  ivideo->vbflags |= TV_HIVISION;
		else if(cr32 & SIS_VB_SCART)     ivideo->vbflags |= TV_SCART;
		else {
			if(cr32 & SIS_VB_SVIDEO)    ivideo->vbflags |= TV_SVIDEO;
			if(cr32 & SIS_VB_COMPOSITE) ivideo->vbflags |= TV_AVIDEO;
		}
	}

	if(!(ivideo->vbflags & (TV_YPBPR | TV_HIVISION))) {
	    if(ivideo->sisfb_tvstd != -1) {
	       ivideo->vbflags &= ~(TV_NTSC | TV_PAL | TV_PALM | TV_PALN | TV_NTSCJ);
	       ivideo->vbflags |= ivideo->sisfb_tvstd;
	    }
	    if(ivideo->vbflags & TV_SCART) {
	       ivideo->vbflags &= ~(TV_NTSC | TV_PALM | TV_PALN | TV_NTSCJ);
	       ivideo->vbflags |= TV_PAL;
	    }
	    if(!(ivideo->vbflags & (TV_PAL | TV_NTSC | TV_PALM | TV_PALN | TV_NTSCJ))) {
		if(ivideo->sisvga_engine == SIS_300_VGA) {
			temp = SiS_GetReg(SISSR, 0x38);
			if(temp & 0x01) ivideo->vbflags |= TV_PAL;
			else		ivideo->vbflags |= TV_NTSC;
		} else if((ivideo->chip <= SIS_315PRO) || (ivideo->chip >= SIS_330)) {
			temp = SiS_GetReg(SISSR, 0x38);
			if(temp & 0x01) ivideo->vbflags |= TV_PAL;
			else		ivideo->vbflags |= TV_NTSC;
		} else {
			temp = SiS_GetReg(SISCR, 0x79);
			if(temp & 0x20)	ivideo->vbflags |= TV_PAL;
			else		ivideo->vbflags |= TV_NTSC;
		}
	    }
	}

	/* Copy forceCRT1 option to CRT1off if option is given */
	if(ivideo->sisfb_forcecrt1 != -1) {
	   ivideo->sisfb_crt1off = (ivideo->sisfb_forcecrt1) ? 0 : 1;
	}
}

/* ------------------ Sensing routines ------------------ */

static bool sisfb_test_DDC1(struct sis_video_info *ivideo)
{
    unsigned short old;
    int count = 48;

    old = SiS_ReadDDC1Bit(&ivideo->SiS_Pr);
    do {
	if(old != SiS_ReadDDC1Bit(&ivideo->SiS_Pr)) break;
    } while(count--);
    return (count != -1);
}

static void sisfb_sense_crt1(struct sis_video_info *ivideo)
{
	bool mustwait = false;
	u8  sr1F, cr17;
#ifdef CONFIG_FB_SIS_315
	u8  cr63 = 0;
#endif
	u16 temp = 0xffff;
	int i;

	sr1F = SiS_GetReg(SISSR, 0x1F);
	SiS_SetRegOR(SISSR, 0x1F, 0x04);
	SiS_SetRegAND(SISSR, 0x1F, 0x3F);

	if (sr1F & 0xc0)
		mustwait = true;

#ifdef CONFIG_FB_SIS_315
	if (ivideo->sisvga_engine == SIS_315_VGA) {
		cr63 = SiS_GetReg(SISCR, ivideo->SiS_Pr.SiS_MyCR63);
		cr63 &= 0x40;
		SiS_SetRegAND(SISCR, ivideo->SiS_Pr.SiS_MyCR63, 0xBF);
	}
#endif

	cr17 = SiS_GetReg(SISCR, 0x17);
	cr17 &= 0x80;

	if (!cr17) {
		SiS_SetRegOR(SISCR, 0x17, 0x80);
		mustwait = true;
		SiS_SetReg(SISSR, 0x00, 0x01);
		SiS_SetReg(SISSR, 0x00, 0x03);
	}

	if (mustwait) {
		for (i = 0; i < 10; i++)
			sisfbwaitretracecrt1(ivideo);
	}
#ifdef CONFIG_FB_SIS_315
	if (ivideo->chip >= SIS_330) {
		SiS_SetRegAND(SISCR, 0x32, ~0x20);
		if (ivideo->chip >= SIS_340)
			SiS_SetReg(SISCR, 0x57, 0x4a);
		else
			SiS_SetReg(SISCR, 0x57, 0x5f);

		SiS_SetRegOR(SISCR, 0x53, 0x02);
		while ((SiS_GetRegByte(SISINPSTAT)) & 0x01)
			break;
		while (!((SiS_GetRegByte(SISINPSTAT)) & 0x01))
			break;
		if ((SiS_GetRegByte(SISMISCW)) & 0x10)
			temp = 1;

		SiS_SetRegAND(SISCR, 0x53, 0xfd);
		SiS_SetRegAND(SISCR, 0x57, 0x00);
	}
#endif

	if (temp == 0xffff) {
		i = 3;

		do {
			temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags,
			ivideo->sisvga_engine, 0, 0, NULL, ivideo->vbflags2);
		} while (((temp == 0) || (temp == 0xffff)) && i--);

		if ((temp == 0) || (temp == 0xffff)) {
			if (sisfb_test_DDC1(ivideo))
				temp = 1;
		}
	}

	if ((temp) && (temp != 0xffff))
		SiS_SetRegOR(SISCR, 0x32, 0x20);

#ifdef CONFIG_FB_SIS_315
	if (ivideo->sisvga_engine == SIS_315_VGA)
		SiS_SetRegANDOR(SISCR, ivideo->SiS_Pr.SiS_MyCR63, 0xBF, cr63);
#endif

	SiS_SetRegANDOR(SISCR, 0x17, 0x7F, cr17);
	SiS_SetReg(SISSR, 0x1F, sr1F);
}

/* Determine and detect attached devices on SiS30x */
static void SiS_SenseLCD(struct sis_video_info *ivideo)
{
	unsigned char buffer[256];
	unsigned short temp, realcrtno, i;
	u8 reg, cr37 = 0, paneltype = 0;
	u16 xres, yres;

	ivideo->SiS_Pr.PanelSelfDetected = false;

	/* LCD detection only for TMDS bridges */
	if (!(ivideo->vbflags2 & VB2_SISTMDSBRIDGE))
		return;
	if (ivideo->vbflags2 & VB2_30xBDH)
		return;

	/* If LCD already set up by BIOS, skip it */
	reg = SiS_GetReg(SISCR, 0x32);
	if (reg & 0x08)
		return;

	realcrtno = 1;
	if (ivideo->SiS_Pr.DDCPortMixup)
		realcrtno = 0;

	/* Check DDC capabilities */
	temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags, ivideo->sisvga_engine,
				realcrtno, 0, &buffer[0], ivideo->vbflags2);

	if ((!temp) || (temp == 0xffff) || (!(temp & 0x02)))
		return;

	/* Read DDC data */
	i = 3;  /* Number of retrys */
	do {
		temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags,
				ivideo->sisvga_engine, realcrtno, 1,
				&buffer[0], ivideo->vbflags2);
	} while ((temp) && i--);

	if (temp)
		return;

	/* No digital device */
	if (!(buffer[0x14] & 0x80))
		return;

	/* First detailed timing preferred timing? */
	if (!(buffer[0x18] & 0x02))
		return;

	xres = buffer[0x38] | ((buffer[0x3a] & 0xf0) << 4);
	yres = buffer[0x3b] | ((buffer[0x3d] & 0xf0) << 4);

	switch(xres) {
		case 1024:
			if (yres == 768)
				paneltype = 0x02;
			break;
		case 1280:
			if (yres == 1024)
				paneltype = 0x03;
			break;
		case 1600:
			if ((yres == 1200) && (ivideo->vbflags2 & VB2_30xC))
				paneltype = 0x0b;
			break;
	}

	if (!paneltype)
		return;

	if (buffer[0x23])
		cr37 |= 0x10;

	if ((buffer[0x47] & 0x18) == 0x18)
		cr37 |= ((((buffer[0x47] & 0x06) ^ 0x06) << 5) | 0x20);
	else
		cr37 |= 0xc0;

	SiS_SetReg(SISCR, 0x36, paneltype);
	cr37 &= 0xf1;
	SiS_SetRegANDOR(SISCR, 0x37, 0x0c, cr37);
	SiS_SetRegOR(SISCR, 0x32, 0x08);

	ivideo->SiS_Pr.PanelSelfDetected = true;
}

static int SISDoSense(struct sis_video_info *ivideo, u16 type, u16 test)
{
	int temp, mytest, result, i, j;

	for (j = 0; j < 10; j++) {
		result = 0;
		for (i = 0; i < 3; i++) {
			mytest = test;
			SiS_SetReg(SISPART4, 0x11, (type & 0x00ff));
			temp = (type >> 8) | (mytest & 0x00ff);
			SiS_SetRegANDOR(SISPART4, 0x10, 0xe0, temp);
			SiS_DDC2Delay(&ivideo->SiS_Pr, 0x1500);
			mytest >>= 8;
			mytest &= 0x7f;
			temp = SiS_GetReg(SISPART4, 0x03);
			temp ^= 0x0e;
			temp &= mytest;
			if (temp == mytest)
				result++;
#if 1
			SiS_SetReg(SISPART4, 0x11, 0x00);
			SiS_SetRegAND(SISPART4, 0x10, 0xe0);
			SiS_DDC2Delay(&ivideo->SiS_Pr, 0x1000);
#endif
		}

		if ((result == 0) || (result >= 2))
			break;
	}
	return result;
}

static void SiS_Sense30x(struct sis_video_info *ivideo)
{
    u8  backupP4_0d,backupP2_00,backupP2_4d,backupSR_1e,biosflag=0;
    u16 svhs=0, svhs_c=0;
    u16 cvbs=0, cvbs_c=0;
    u16 vga2=0, vga2_c=0;
    int myflag, result;
    char stdstr[] = "sisfb: Detected";
    char tvstr[]  = "TV connected to";

    if(ivideo->vbflags2 & VB2_301) {
       svhs = 0x00b9; cvbs = 0x00b3; vga2 = 0x00d1;
       myflag = SiS_GetReg(SISPART4, 0x01);
       if(myflag & 0x04) {
	  svhs = 0x00dd; cvbs = 0x00ee; vga2 = 0x00fd;
       }
    } else if(ivideo->vbflags2 & (VB2_301B | VB2_302B)) {
       svhs = 0x016b; cvbs = 0x0174; vga2 = 0x0190;
    } else if(ivideo->vbflags2 & (VB2_301LV | VB2_302LV)) {
       svhs = 0x0200; cvbs = 0x0100;
    } else if(ivideo->vbflags2 & (VB2_301C | VB2_302ELV | VB2_307T | VB2_307LV)) {
       svhs = 0x016b; cvbs = 0x0110; vga2 = 0x0190;
    } else
       return;

    vga2_c = 0x0e08; svhs_c = 0x0404; cvbs_c = 0x0804;
    if(ivideo->vbflags & (VB2_301LV|VB2_302LV|VB2_302ELV|VB2_307LV)) {
       svhs_c = 0x0408; cvbs_c = 0x0808;
    }

    biosflag = 2;
    if(ivideo->haveXGIROM) {
       biosflag = ivideo->bios_abase[0x58] & 0x03;
    } else if(ivideo->newrom) {
       if(ivideo->bios_abase[0x5d] & 0x04) biosflag |= 0x01;
    } else if(ivideo->sisvga_engine == SIS_300_VGA) {
       if(ivideo->bios_abase) {
          biosflag = ivideo->bios_abase[0xfe] & 0x03;
       }
    }

    if(ivideo->chip == SIS_300) {
       myflag = SiS_GetReg(SISSR, 0x3b);
       if(!(myflag & 0x01)) vga2 = vga2_c = 0;
    }

    if(!(ivideo->vbflags2 & VB2_SISVGA2BRIDGE)) {
       vga2 = vga2_c = 0;
    }

    backupSR_1e = SiS_GetReg(SISSR, 0x1e);
    SiS_SetRegOR(SISSR, 0x1e, 0x20);

    backupP4_0d = SiS_GetReg(SISPART4, 0x0d);
    if(ivideo->vbflags2 & VB2_30xC) {
	SiS_SetRegANDOR(SISPART4, 0x0d, ~0x07, 0x01);
    } else {
       SiS_SetRegOR(SISPART4, 0x0d, 0x04);
    }
    SiS_DDC2Delay(&ivideo->SiS_Pr, 0x2000);

    backupP2_00 = SiS_GetReg(SISPART2, 0x00);
    SiS_SetReg(SISPART2, 0x00, ((backupP2_00 | 0x1c) & 0xfc));

    backupP2_4d = SiS_GetReg(SISPART2, 0x4d);
    if(ivideo->vbflags2 & VB2_SISYPBPRBRIDGE) {
	SiS_SetReg(SISPART2, 0x4d, (backupP2_4d & ~0x10));
    }

    if(!(ivideo->vbflags2 & VB2_30xCLV)) {
       SISDoSense(ivideo, 0, 0);
    }

    SiS_SetRegAND(SISCR, 0x32, ~0x14);

    if(vga2_c || vga2) {
       if(SISDoSense(ivideo, vga2, vga2_c)) {
          if(biosflag & 0x01) {
	     printk(KERN_INFO "%s %s SCART output\n", stdstr, tvstr);
	     SiS_SetRegOR(SISCR, 0x32, 0x04);
	  } else {
	     printk(KERN_INFO "%s secondary VGA connection\n", stdstr);
	     SiS_SetRegOR(SISCR, 0x32, 0x10);
	  }
       }
    }

    SiS_SetRegAND(SISCR, 0x32, 0x3f);

    if(ivideo->vbflags2 & VB2_30xCLV) {
       SiS_SetRegOR(SISPART4, 0x0d, 0x04);
    }

    if((ivideo->sisvga_engine == SIS_315_VGA) && (ivideo->vbflags2 & VB2_SISYPBPRBRIDGE)) {
       SiS_SetReg(SISPART2, 0x4d, (backupP2_4d | 0x10));
       SiS_DDC2Delay(&ivideo->SiS_Pr, 0x2000);
       if((result = SISDoSense(ivideo, svhs, 0x0604))) {
          if((result = SISDoSense(ivideo, cvbs, 0x0804))) {
	     printk(KERN_INFO "%s %s YPbPr component output\n", stdstr, tvstr);
	     SiS_SetRegOR(SISCR, 0x32, 0x80);
	  }
       }
       SiS_SetReg(SISPART2, 0x4d, backupP2_4d);
    }

    SiS_SetRegAND(SISCR, 0x32, ~0x03);

    if(!(ivideo->vbflags & TV_YPBPR)) {
       if((result = SISDoSense(ivideo, svhs, svhs_c))) {
          printk(KERN_INFO "%s %s SVIDEO output\n", stdstr, tvstr);
	   SiS_SetRegOR(SISCR, 0x32, 0x02);
       }
       if((biosflag & 0x02) || (!result)) {
          if(SISDoSense(ivideo, cvbs, cvbs_c)) {
	     printk(KERN_INFO "%s %s COMPOSITE output\n", stdstr, tvstr);
	     SiS_SetRegOR(SISCR, 0x32, 0x01);
          }
       }
    }

    SISDoSense(ivideo, 0, 0);

    SiS_SetReg(SISPART2, 0x00, backupP2_00);
    SiS_SetReg(SISPART4, 0x0d, backupP4_0d);
    SiS_SetReg(SISSR, 0x1e, backupSR_1e);

    if(ivideo->vbflags2 & VB2_30xCLV) {
	biosflag = SiS_GetReg(SISPART2, 0x00);
       if(biosflag & 0x20) {
          for(myflag = 2; myflag > 0; myflag--) {
	     biosflag ^= 0x20;
	     SiS_SetReg(SISPART2, 0x00, biosflag);
	  }
       }
    }

    SiS_SetReg(SISPART2, 0x00, backupP2_00);
}

/* Determine and detect attached TV's on Chrontel */
static void SiS_SenseCh(struct sis_video_info *ivideo)
{
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
    u8 temp1, temp2;
    char stdstr[] = "sisfb: Chrontel: Detected TV connected to";
#endif
#ifdef CONFIG_FB_SIS_300
    unsigned char test[3];
    int i;
#endif

    if(ivideo->chip < SIS_315H) {

#ifdef CONFIG_FB_SIS_300
       ivideo->SiS_Pr.SiS_IF_DEF_CH70xx = 1;		/* Chrontel 700x */
       SiS_SetChrontelGPIO(&ivideo->SiS_Pr, 0x9c);	/* Set general purpose IO for Chrontel communication */
       SiS_DDC2Delay(&ivideo->SiS_Pr, 1000);
       temp1 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x25);
       /* See Chrontel TB31 for explanation */
       temp2 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x0e);
       if(((temp2 & 0x07) == 0x01) || (temp2 & 0x04)) {
	  SiS_SetCH700x(&ivideo->SiS_Pr, 0x0e, 0x0b);
	  SiS_DDC2Delay(&ivideo->SiS_Pr, 300);
       }
       temp2 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x25);
       if(temp2 != temp1) temp1 = temp2;

       if((temp1 >= 0x22) && (temp1 <= 0x50)) {
	   /* Read power status */
	   temp1 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x0e);
	   if((temp1 & 0x03) != 0x03) {
		/* Power all outputs */
		SiS_SetCH700x(&ivideo->SiS_Pr, 0x0e,0x0b);
		SiS_DDC2Delay(&ivideo->SiS_Pr, 300);
	   }
	   /* Sense connected TV devices */
	   for(i = 0; i < 3; i++) {
	       SiS_SetCH700x(&ivideo->SiS_Pr, 0x10, 0x01);
	       SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	       SiS_SetCH700x(&ivideo->SiS_Pr, 0x10, 0x00);
	       SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	       temp1 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x10);
	       if(!(temp1 & 0x08))       test[i] = 0x02;
	       else if(!(temp1 & 0x02))  test[i] = 0x01;
	       else                      test[i] = 0;
	       SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	   }

	   if(test[0] == test[1])      temp1 = test[0];
	   else if(test[0] == test[2]) temp1 = test[0];
	   else if(test[1] == test[2]) temp1 = test[1];
	   else {
		printk(KERN_INFO
			"sisfb: TV detection unreliable - test results varied\n");
		temp1 = test[2];
	   }
	   if(temp1 == 0x02) {
		printk(KERN_INFO "%s SVIDEO output\n", stdstr);
		ivideo->vbflags |= TV_SVIDEO;
		SiS_SetRegOR(SISCR, 0x32, 0x02);
		SiS_SetRegAND(SISCR, 0x32, ~0x05);
	   } else if (temp1 == 0x01) {
		printk(KERN_INFO "%s CVBS output\n", stdstr);
		ivideo->vbflags |= TV_AVIDEO;
		SiS_SetRegOR(SISCR, 0x32, 0x01);
		SiS_SetRegAND(SISCR, 0x32, ~0x06);
	   } else {
		SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, 0x0e, 0x01, 0xF8);
		SiS_SetRegAND(SISCR, 0x32, ~0x07);
	   }
       } else if(temp1 == 0) {
	  SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, 0x0e, 0x01, 0xF8);
	  SiS_SetRegAND(SISCR, 0x32, ~0x07);
       }
       /* Set general purpose IO for Chrontel communication */
       SiS_SetChrontelGPIO(&ivideo->SiS_Pr, 0x00);
#endif

    } else {

#ifdef CONFIG_FB_SIS_315
	ivideo->SiS_Pr.SiS_IF_DEF_CH70xx = 2;		/* Chrontel 7019 */
	temp1 = SiS_GetCH701x(&ivideo->SiS_Pr, 0x49);
	SiS_SetCH701x(&ivideo->SiS_Pr, 0x49, 0x20);
	SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	temp2 = SiS_GetCH701x(&ivideo->SiS_Pr, 0x20);
	temp2 |= 0x01;
	SiS_SetCH701x(&ivideo->SiS_Pr, 0x20, temp2);
	SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	temp2 ^= 0x01;
	SiS_SetCH701x(&ivideo->SiS_Pr, 0x20, temp2);
	SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	temp2 = SiS_GetCH701x(&ivideo->SiS_Pr, 0x20);
	SiS_SetCH701x(&ivideo->SiS_Pr, 0x49, temp1);
	temp1 = 0;
	if(temp2 & 0x02) temp1 |= 0x01;
	if(temp2 & 0x10) temp1 |= 0x01;
	if(temp2 & 0x04) temp1 |= 0x02;
	if( (temp1 & 0x01) && (temp1 & 0x02) ) temp1 = 0x04;
	switch(temp1) {
	case 0x01:
	     printk(KERN_INFO "%s CVBS output\n", stdstr);
	     ivideo->vbflags |= TV_AVIDEO;
	     SiS_SetRegOR(SISCR, 0x32, 0x01);
	     SiS_SetRegAND(SISCR, 0x32, ~0x06);
	     break;
	case 0x02:
	     printk(KERN_INFO "%s SVIDEO output\n", stdstr);
	     ivideo->vbflags |= TV_SVIDEO;
	     SiS_SetRegOR(SISCR, 0x32, 0x02);
	     SiS_SetRegAND(SISCR, 0x32, ~0x05);
	     break;
	case 0x04:
	     printk(KERN_INFO "%s SCART output\n", stdstr);
	     SiS_SetRegOR(SISCR, 0x32, 0x04);
	     SiS_SetRegAND(SISCR, 0x32, ~0x03);
	     break;
	default:
	     SiS_SetRegAND(SISCR, 0x32, ~0x07);
	}
#endif
    }
}

static void sisfb_get_VB_type(struct sis_video_info *ivideo)
{
	char stdstr[]    = "sisfb: Detected";
	char bridgestr[] = "video bridge";
	u8 vb_chipid;
	u8 reg;

	/* No CRT2 on XGI Z7 */
	if(ivideo->chip == XGI_20)
		return;

	vb_chipid = SiS_GetReg(SISPART4, 0x00);
	switch(vb_chipid) {
	case 0x01:
		reg = SiS_GetReg(SISPART4, 0x01);
		if(reg < 0xb0) {
			ivideo->vbflags |= VB_301;	/* Deprecated */
			ivideo->vbflags2 |= VB2_301;
			printk(KERN_INFO "%s SiS301 %s\n", stdstr, bridgestr);
		} else if(reg < 0xc0) {
			ivideo->vbflags |= VB_301B;	/* Deprecated */
			ivideo->vbflags2 |= VB2_301B;
			reg = SiS_GetReg(SISPART4, 0x23);
			if(!(reg & 0x02)) {
			   ivideo->vbflags |= VB_30xBDH;	/* Deprecated */
			   ivideo->vbflags2 |= VB2_30xBDH;
			   printk(KERN_INFO "%s SiS301B-DH %s\n", stdstr, bridgestr);
			} else {
			   printk(KERN_INFO "%s SiS301B %s\n", stdstr, bridgestr);
			}
		} else if(reg < 0xd0) {
			ivideo->vbflags |= VB_301C;	/* Deprecated */
			ivideo->vbflags2 |= VB2_301C;
			printk(KERN_INFO "%s SiS301C %s\n", stdstr, bridgestr);
		} else if(reg < 0xe0) {
			ivideo->vbflags |= VB_301LV;	/* Deprecated */
			ivideo->vbflags2 |= VB2_301LV;
			printk(KERN_INFO "%s SiS301LV %s\n", stdstr, bridgestr);
		} else if(reg <= 0xe1) {
			reg = SiS_GetReg(SISPART4, 0x39);
			if(reg == 0xff) {
			   ivideo->vbflags |= VB_302LV;	/* Deprecated */
			   ivideo->vbflags2 |= VB2_302LV;
			   printk(KERN_INFO "%s SiS302LV %s\n", stdstr, bridgestr);
			} else {
			   ivideo->vbflags |= VB_301C;	/* Deprecated */
			   ivideo->vbflags2 |= VB2_301C;
			   printk(KERN_INFO "%s SiS301C(P4) %s\n", stdstr, bridgestr);
#if 0
			   ivideo->vbflags |= VB_302ELV;	/* Deprecated */
			   ivideo->vbflags2 |= VB2_302ELV;
			   printk(KERN_INFO "%s SiS302ELV %s\n", stdstr, bridgestr);
#endif
			}
		}
		break;
	case 0x02:
		ivideo->vbflags |= VB_302B;	/* Deprecated */
		ivideo->vbflags2 |= VB2_302B;
		printk(KERN_INFO "%s SiS302B %s\n", stdstr, bridgestr);
		break;
	}

	if((!(ivideo->vbflags2 & VB2_VIDEOBRIDGE)) && (ivideo->chip != SIS_300)) {
		reg = SiS_GetReg(SISCR, 0x37);
		reg &= SIS_EXTERNAL_CHIP_MASK;
		reg >>= 1;
		if(ivideo->sisvga_engine == SIS_300_VGA) {
#ifdef CONFIG_FB_SIS_300
			switch(reg) {
			   case SIS_EXTERNAL_CHIP_LVDS:
				ivideo->vbflags |= VB_LVDS;	/* Deprecated */
				ivideo->vbflags2 |= VB2_LVDS;
				break;
			   case SIS_EXTERNAL_CHIP_TRUMPION:
				ivideo->vbflags |= (VB_LVDS | VB_TRUMPION);	/* Deprecated */
				ivideo->vbflags2 |= (VB2_LVDS | VB2_TRUMPION);
				break;
			   case SIS_EXTERNAL_CHIP_CHRONTEL:
				ivideo->vbflags |= VB_CHRONTEL;	/* Deprecated */
				ivideo->vbflags2 |= VB2_CHRONTEL;
				break;
			   case SIS_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo->vbflags |= (VB_LVDS | VB_CHRONTEL);	/* Deprecated */
				ivideo->vbflags2 |= (VB2_LVDS | VB2_CHRONTEL);
				break;
			}
			if(ivideo->vbflags2 & VB2_CHRONTEL) ivideo->chronteltype = 1;
#endif
		} else if(ivideo->chip < SIS_661) {
#ifdef CONFIG_FB_SIS_315
			switch (reg) {
			   case SIS310_EXTERNAL_CHIP_LVDS:
				ivideo->vbflags |= VB_LVDS;	/* Deprecated */
				ivideo->vbflags2 |= VB2_LVDS;
				break;
			   case SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo->vbflags |= (VB_LVDS | VB_CHRONTEL);	/* Deprecated */
				ivideo->vbflags2 |= (VB2_LVDS | VB2_CHRONTEL);
				break;
			}
			if(ivideo->vbflags2 & VB2_CHRONTEL) ivideo->chronteltype = 2;
#endif
		} else if(ivideo->chip >= SIS_661) {
#ifdef CONFIG_FB_SIS_315
			reg = SiS_GetReg(SISCR, 0x38);
			reg >>= 5;
			switch(reg) {
			   case 0x02:
				ivideo->vbflags |= VB_LVDS;	/* Deprecated */
				ivideo->vbflags2 |= VB2_LVDS;
				break;
			   case 0x03:
				ivideo->vbflags |= (VB_LVDS | VB_CHRONTEL);	/* Deprecated */
				ivideo->vbflags2 |= (VB2_LVDS | VB2_CHRONTEL);
				break;
			   case 0x04:
				ivideo->vbflags |= (VB_LVDS | VB_CONEXANT);	/* Deprecated */
				ivideo->vbflags2 |= (VB2_LVDS | VB2_CONEXANT);
				break;
			}
			if(ivideo->vbflags2 & VB2_CHRONTEL) ivideo->chronteltype = 2;
#endif
		}
		if(ivideo->vbflags2 & VB2_LVDS) {
		   printk(KERN_INFO "%s LVDS transmitter\n", stdstr);
		}
		if((ivideo->sisvga_engine == SIS_300_VGA) && (ivideo->vbflags2 & VB2_TRUMPION)) {
		   printk(KERN_INFO "%s Trumpion Zurac LCD scaler\n", stdstr);
		}
		if(ivideo->vbflags2 & VB2_CHRONTEL) {
		   printk(KERN_INFO "%s Chrontel TV encoder\n", stdstr);
		}
		if((ivideo->chip >= SIS_661) && (ivideo->vbflags2 & VB2_CONEXANT)) {
		   printk(KERN_INFO "%s Conexant external device\n", stdstr);
		}
	}

	if(ivideo->vbflags2 & VB2_SISBRIDGE) {
		SiS_SenseLCD(ivideo);
		SiS_Sense30x(ivideo);
	} else if(ivideo->vbflags2 & VB2_CHRONTEL) {
		SiS_SenseCh(ivideo);
	}
}

/* ---------- Engine initialization routines ------------ */

static void
sisfb_engine_init(struct sis_video_info *ivideo)
{

	/* Initialize command queue (we use MMIO only) */

	/* BEFORE THIS IS CALLED, THE ENGINES *MUST* BE SYNC'ED */

	ivideo->caps &= ~(TURBO_QUEUE_CAP    |
			  MMIO_CMD_QUEUE_CAP |
			  VM_CMD_QUEUE_CAP   |
			  AGP_CMD_QUEUE_CAP);

#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		u32 tqueue_pos;
		u8 tq_state;

		tqueue_pos = (ivideo->video_size - ivideo->cmdQueueSize) / (64 * 1024);

		tq_state = SiS_GetReg(SISSR, IND_SIS_TURBOQUEUE_SET);
		tq_state |= 0xf0;
		tq_state &= 0xfc;
		tq_state |= (u8)(tqueue_pos >> 8);
		SiS_SetReg(SISSR, IND_SIS_TURBOQUEUE_SET, tq_state);

		SiS_SetReg(SISSR, IND_SIS_TURBOQUEUE_ADR, (u8)(tqueue_pos & 0xff));

		ivideo->caps |= TURBO_QUEUE_CAP;
	}
#endif

#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
		u32 tempq = 0, templ;
		u8  temp;

		if(ivideo->chip == XGI_20) {
			switch(ivideo->cmdQueueSize) {
			case (64 * 1024):
				temp = SIS_CMD_QUEUE_SIZE_Z7_64k;
				break;
			case (128 * 1024):
			default:
				temp = SIS_CMD_QUEUE_SIZE_Z7_128k;
			}
		} else {
			switch(ivideo->cmdQueueSize) {
			case (4 * 1024 * 1024):
				temp = SIS_CMD_QUEUE_SIZE_4M;
				break;
			case (2 * 1024 * 1024):
				temp = SIS_CMD_QUEUE_SIZE_2M;
				break;
			case (1 * 1024 * 1024):
				temp = SIS_CMD_QUEUE_SIZE_1M;
				break;
			default:
			case (512 * 1024):
				temp = SIS_CMD_QUEUE_SIZE_512k;
			}
		}

		SiS_SetReg(SISSR, IND_SIS_CMDQUEUE_THRESHOLD, COMMAND_QUEUE_THRESHOLD);
		SiS_SetReg(SISSR, IND_SIS_CMDQUEUE_SET, SIS_CMD_QUEUE_RESET);

		if((ivideo->chip >= XGI_40) && ivideo->modechanged) {
			/* Must disable dual pipe on XGI_40. Can't do
			 * this in MMIO mode, because it requires
			 * setting/clearing a bit in the MMIO fire trigger
			 * register.
			 */
			if(!((templ = MMIO_IN32(ivideo->mmio_vbase, 0x8240)) & (1 << 10))) {

				MMIO_OUT32(ivideo->mmio_vbase, Q_WRITE_PTR, 0);

				SiS_SetReg(SISSR, IND_SIS_CMDQUEUE_SET, (temp | SIS_VRAM_CMDQUEUE_ENABLE));

				tempq = MMIO_IN32(ivideo->mmio_vbase, Q_READ_PTR);
				MMIO_OUT32(ivideo->mmio_vbase, Q_WRITE_PTR, tempq);

				tempq = (u32)(ivideo->video_size - ivideo->cmdQueueSize);
				MMIO_OUT32(ivideo->mmio_vbase, Q_BASE_ADDR, tempq);

				writel(0x16800000 + 0x8240, ivideo->video_vbase + tempq);
				writel(templ | (1 << 10), ivideo->video_vbase + tempq + 4);
				writel(0x168F0000, ivideo->video_vbase + tempq + 8);
				writel(0x168F0000, ivideo->video_vbase + tempq + 12);

				MMIO_OUT32(ivideo->mmio_vbase, Q_WRITE_PTR, (tempq + 16));

				sisfb_syncaccel(ivideo);

				SiS_SetReg(SISSR, IND_SIS_CMDQUEUE_SET, SIS_CMD_QUEUE_RESET);

			}
		}

		tempq = MMIO_IN32(ivideo->mmio_vbase, MMIO_QUEUE_READPORT);
		MMIO_OUT32(ivideo->mmio_vbase, MMIO_QUEUE_WRITEPORT, tempq);

		temp |= (SIS_MMIO_CMD_ENABLE | SIS_CMD_AUTO_CORR);
		SiS_SetReg(SISSR, IND_SIS_CMDQUEUE_SET, temp);

		tempq = (u32)(ivideo->video_size - ivideo->cmdQueueSize);
		MMIO_OUT32(ivideo->mmio_vbase, MMIO_QUEUE_PHYBASE, tempq);

		ivideo->caps |= MMIO_CMD_QUEUE_CAP;
	}
#endif

	ivideo->engineok = 1;
}

static void sisfb_detect_lcd_type(struct sis_video_info *ivideo)
{
	u8 reg;
	int i;

	reg = SiS_GetReg(SISCR, 0x36);
	reg &= 0x0f;
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		ivideo->CRT2LCDType = sis300paneltype[reg];
	} else if(ivideo->chip >= SIS_661) {
		ivideo->CRT2LCDType = sis661paneltype[reg];
	} else {
		ivideo->CRT2LCDType = sis310paneltype[reg];
		if((ivideo->chip == SIS_550) && (sisfb_fstn)) {
			if((ivideo->CRT2LCDType != LCD_320x240_2) &&
			   (ivideo->CRT2LCDType != LCD_320x240_3)) {
				ivideo->CRT2LCDType = LCD_320x240;
			}
		}
	}

	if(ivideo->CRT2LCDType == LCD_UNKNOWN) {
		/* For broken BIOSes: Assume 1024x768, RGB18 */
		ivideo->CRT2LCDType = LCD_1024x768;
		SiS_SetRegANDOR(SISCR, 0x36, 0xf0, 0x02);
		SiS_SetRegANDOR(SISCR, 0x37, 0xee, 0x01);
		printk(KERN_DEBUG "sisfb: Invalid panel ID (%02x), assuming 1024x768, RGB18\n", reg);
	}

	for(i = 0; i < SIS_LCD_NUMBER; i++) {
		if(ivideo->CRT2LCDType == sis_lcd_data[i].lcdtype) {
			ivideo->lcdxres = sis_lcd_data[i].xres;
			ivideo->lcdyres = sis_lcd_data[i].yres;
			ivideo->lcddefmodeidx = sis_lcd_data[i].default_mode_idx;
			break;
		}
	}

#ifdef CONFIG_FB_SIS_300
	if(ivideo->SiS_Pr.SiS_CustomT == CUT_BARCO1366) {
		ivideo->lcdxres = 1360; ivideo->lcdyres = 1024;
		ivideo->lcddefmodeidx = DEFAULT_MODE_1360;
	} else if(ivideo->SiS_Pr.SiS_CustomT == CUT_PANEL848) {
		ivideo->lcdxres =  848; ivideo->lcdyres =  480;
		ivideo->lcddefmodeidx = DEFAULT_MODE_848;
	} else if(ivideo->SiS_Pr.SiS_CustomT == CUT_PANEL856) {
		ivideo->lcdxres =  856; ivideo->lcdyres =  480;
		ivideo->lcddefmodeidx = DEFAULT_MODE_856;
	}
#endif

	printk(KERN_DEBUG "sisfb: Detected %dx%d flat panel\n",
			ivideo->lcdxres, ivideo->lcdyres);
}

static void sisfb_save_pdc_emi(struct sis_video_info *ivideo)
{
#ifdef CONFIG_FB_SIS_300
	/* Save the current PanelDelayCompensation if the LCD is currently used */
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		if(ivideo->vbflags2 & (VB2_LVDS | VB2_30xBDH)) {
			int tmp;
			tmp = SiS_GetReg(SISCR, 0x30);
			if(tmp & 0x20) {
				/* Currently on LCD? If yes, read current pdc */
				ivideo->detectedpdc = SiS_GetReg(SISPART1, 0x13);
				ivideo->detectedpdc &= 0x3c;
				if(ivideo->SiS_Pr.PDC == -1) {
					/* Let option override detection */
					ivideo->SiS_Pr.PDC = ivideo->detectedpdc;
				}
				printk(KERN_INFO "sisfb: Detected LCD PDC 0x%02x\n",
					ivideo->detectedpdc);
			}
			if((ivideo->SiS_Pr.PDC != -1) &&
			   (ivideo->SiS_Pr.PDC != ivideo->detectedpdc)) {
				printk(KERN_INFO "sisfb: Using LCD PDC 0x%02x\n",
					ivideo->SiS_Pr.PDC);
			}
		}
	}
#endif

#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {

		/* Try to find about LCDA */
		if(ivideo->vbflags2 & VB2_SISLCDABRIDGE) {
			int tmp;
			tmp = SiS_GetReg(SISPART1, 0x13);
			if(tmp & 0x04) {
				ivideo->SiS_Pr.SiS_UseLCDA = true;
				ivideo->detectedlcda = 0x03;
			}
		}

		/* Save PDC */
		if(ivideo->vbflags2 & VB2_SISLVDSBRIDGE) {
			int tmp;
			tmp = SiS_GetReg(SISCR, 0x30);
			if((tmp & 0x20) || (ivideo->detectedlcda != 0xff)) {
				/* Currently on LCD? If yes, read current pdc */
				u8 pdc;
				pdc = SiS_GetReg(SISPART1, 0x2D);
				ivideo->detectedpdc  = (pdc & 0x0f) << 1;
				ivideo->detectedpdca = (pdc & 0xf0) >> 3;
				pdc = SiS_GetReg(SISPART1, 0x35);
				ivideo->detectedpdc |= ((pdc >> 7) & 0x01);
				pdc = SiS_GetReg(SISPART1, 0x20);
				ivideo->detectedpdca |= ((pdc >> 6) & 0x01);
				if(ivideo->newrom) {
					/* New ROM invalidates other PDC resp. */
					if(ivideo->detectedlcda != 0xff) {
						ivideo->detectedpdc = 0xff;
					} else {
						ivideo->detectedpdca = 0xff;
					}
				}
				if(ivideo->SiS_Pr.PDC == -1) {
					if(ivideo->detectedpdc != 0xff) {
						ivideo->SiS_Pr.PDC = ivideo->detectedpdc;
					}
				}
				if(ivideo->SiS_Pr.PDCA == -1) {
					if(ivideo->detectedpdca != 0xff) {
						ivideo->SiS_Pr.PDCA = ivideo->detectedpdca;
					}
				}
				if(ivideo->detectedpdc != 0xff) {
					printk(KERN_INFO
						"sisfb: Detected LCD PDC 0x%02x (for LCD=CRT2)\n",
						ivideo->detectedpdc);
				}
				if(ivideo->detectedpdca != 0xff) {
					printk(KERN_INFO
						"sisfb: Detected LCD PDC1 0x%02x (for LCD=CRT1)\n",
						ivideo->detectedpdca);
				}
			}

			/* Save EMI */
			if(ivideo->vbflags2 & VB2_SISEMIBRIDGE) {
				ivideo->SiS_Pr.EMI_30 = SiS_GetReg(SISPART4, 0x30);
				ivideo->SiS_Pr.EMI_31 = SiS_GetReg(SISPART4, 0x31);
				ivideo->SiS_Pr.EMI_32 = SiS_GetReg(SISPART4, 0x32);
				ivideo->SiS_Pr.EMI_33 = SiS_GetReg(SISPART4, 0x33);
				ivideo->SiS_Pr.HaveEMI = true;
				if((tmp & 0x20) || (ivideo->detectedlcda != 0xff)) {
					ivideo->SiS_Pr.HaveEMILCD = true;
				}
			}
		}

		/* Let user override detected PDCs (all bridges) */
		if(ivideo->vbflags2 & VB2_30xBLV) {
			if((ivideo->SiS_Pr.PDC != -1) &&
			   (ivideo->SiS_Pr.PDC != ivideo->detectedpdc)) {
				printk(KERN_INFO "sisfb: Using LCD PDC 0x%02x (for LCD=CRT2)\n",
					ivideo->SiS_Pr.PDC);
			}
			if((ivideo->SiS_Pr.PDCA != -1) &&
			   (ivideo->SiS_Pr.PDCA != ivideo->detectedpdca)) {
				printk(KERN_INFO "sisfb: Using LCD PDC1 0x%02x (for LCD=CRT1)\n",
				 ivideo->SiS_Pr.PDCA);
			}
		}

	}
#endif
}

/* -------------------- Memory manager routines ---------------------- */

static u32 sisfb_getheapstart(struct sis_video_info *ivideo)
{
	u32 ret = ivideo->sisfb_parm_mem * 1024;
	u32 maxoffs = ivideo->video_size - ivideo->hwcursor_size - ivideo->cmdQueueSize;
	u32 def;

	/* Calculate heap start = end of memory for console
	 *
	 * CCCCCCCCDDDDDDDDDDDDDDDDDDDDDDDDDDDDHHHHQQQQQQQQQQ
	 * C = console, D = heap, H = HWCursor, Q = cmd-queue
	 *
	 * On 76x in UMA+LFB mode, the layout is as follows:
	 * DDDDDDDDDDDCCCCCCCCCCCCCCCCCCCCCCCCHHHHQQQQQQQQQQQ
	 * where the heap is the entire UMA area, eventually
	 * into the LFB area if the given mem parameter is
	 * higher than the size of the UMA memory.
	 *
	 * Basically given by "mem" parameter
	 *
	 * maximum = videosize - cmd_queue - hwcursor
	 *           (results in a heap of size 0)
	 * default = SiS 300: depends on videosize
	 *           SiS 315/330/340/XGI: 32k below max
	 */

	if(ivideo->sisvga_engine == SIS_300_VGA) {
		if(ivideo->video_size > 0x1000000) {
			def = 0xc00000;
		} else if(ivideo->video_size > 0x800000) {
			def = 0x800000;
		} else {
			def = 0x400000;
		}
	} else if(ivideo->UMAsize && ivideo->LFBsize) {
		ret = def = 0;
	} else {
		def = maxoffs - 0x8000;
	}

	/* Use default for secondary card for now (FIXME) */
	if((!ret) || (ret > maxoffs) || (ivideo->cardnumber != 0))
		ret = def;

	return ret;
}

static u32 sisfb_getheapsize(struct sis_video_info *ivideo)
{
	u32 max = ivideo->video_size - ivideo->hwcursor_size - ivideo->cmdQueueSize;
	u32 ret = 0;

	if(ivideo->UMAsize && ivideo->LFBsize) {
		if( (!ivideo->sisfb_parm_mem)			||
		    ((ivideo->sisfb_parm_mem * 1024) > max)	||
		    ((max - (ivideo->sisfb_parm_mem * 1024)) < ivideo->UMAsize) ) {
			ret = ivideo->UMAsize;
			max -= ivideo->UMAsize;
		} else {
			ret = max - (ivideo->sisfb_parm_mem * 1024);
			max = ivideo->sisfb_parm_mem * 1024;
		}
		ivideo->video_offset = ret;
		ivideo->sisfb_mem = max;
	} else {
		ret = max - ivideo->heapstart;
		ivideo->sisfb_mem = ivideo->heapstart;
	}

	return ret;
}

static int sisfb_heap_init(struct sis_video_info *ivideo)
{
	struct SIS_OH *poh;

	ivideo->video_offset = 0;
	if(ivideo->sisfb_parm_mem) {
		if( (ivideo->sisfb_parm_mem < (2 * 1024 * 1024)) ||
		    (ivideo->sisfb_parm_mem > ivideo->video_size) ) {
			ivideo->sisfb_parm_mem = 0;
		}
	}

	ivideo->heapstart = sisfb_getheapstart(ivideo);
	ivideo->sisfb_heap_size = sisfb_getheapsize(ivideo);

	ivideo->sisfb_heap_start = ivideo->video_vbase + ivideo->heapstart;
	ivideo->sisfb_heap_end   = ivideo->sisfb_heap_start + ivideo->sisfb_heap_size;

	printk(KERN_INFO "sisfb: Memory heap starting at %dK, size %dK\n",
		(int)(ivideo->heapstart / 1024), (int)(ivideo->sisfb_heap_size / 1024));

	ivideo->sisfb_heap.vinfo = ivideo;

	ivideo->sisfb_heap.poha_chain = NULL;
	ivideo->sisfb_heap.poh_freelist = NULL;

	poh = sisfb_poh_new_node(&ivideo->sisfb_heap);
	if(poh == NULL)
		return 1;

	poh->poh_next = &ivideo->sisfb_heap.oh_free;
	poh->poh_prev = &ivideo->sisfb_heap.oh_free;
	poh->size = ivideo->sisfb_heap_size;
	poh->offset = ivideo->heapstart;

	ivideo->sisfb_heap.oh_free.poh_next = poh;
	ivideo->sisfb_heap.oh_free.poh_prev = poh;
	ivideo->sisfb_heap.oh_free.size = 0;
	ivideo->sisfb_heap.max_freesize = poh->size;

	ivideo->sisfb_heap.oh_used.poh_next = &ivideo->sisfb_heap.oh_used;
	ivideo->sisfb_heap.oh_used.poh_prev = &ivideo->sisfb_heap.oh_used;
	ivideo->sisfb_heap.oh_used.size = SENTINEL;

	if(ivideo->cardnumber == 0) {
		/* For the first card, make this heap the "global" one
		 * for old DRM (which could handle only one card)
		 */
		sisfb_heap = &ivideo->sisfb_heap;
	}

	return 0;
}

static struct SIS_OH *
sisfb_poh_new_node(struct SIS_HEAP *memheap)
{
	struct SIS_OHALLOC	*poha;
	struct SIS_OH		*poh;
	unsigned long		cOhs;
	int			i;

	if(memheap->poh_freelist == NULL) {
		poha = kmalloc(SIS_OH_ALLOC_SIZE, GFP_KERNEL);
		if(!poha)
			return NULL;

		poha->poha_next = memheap->poha_chain;
		memheap->poha_chain = poha;

		cOhs = (SIS_OH_ALLOC_SIZE - sizeof(struct SIS_OHALLOC)) / sizeof(struct SIS_OH) + 1;

		poh = &poha->aoh[0];
		for(i = cOhs - 1; i != 0; i--) {
			poh->poh_next = poh + 1;
			poh = poh + 1;
		}

		poh->poh_next = NULL;
		memheap->poh_freelist = &poha->aoh[0];
	}

	poh = memheap->poh_freelist;
	memheap->poh_freelist = poh->poh_next;

	return poh;
}

static struct SIS_OH *
sisfb_poh_allocate(struct SIS_HEAP *memheap, u32 size)
{
	struct SIS_OH	*pohThis;
	struct SIS_OH	*pohRoot;
	int		bAllocated = 0;

	if(size > memheap->max_freesize) {
		DPRINTK("sisfb: Can't allocate %dk video memory\n",
			(unsigned int) size / 1024);
		return NULL;
	}

	pohThis = memheap->oh_free.poh_next;

	while(pohThis != &memheap->oh_free) {
		if(size <= pohThis->size) {
			bAllocated = 1;
			break;
		}
		pohThis = pohThis->poh_next;
	}

	if(!bAllocated) {
		DPRINTK("sisfb: Can't allocate %dk video memory\n",
			(unsigned int) size / 1024);
		return NULL;
	}

	if(size == pohThis->size) {
		pohRoot = pohThis;
		sisfb_delete_node(pohThis);
	} else {
		pohRoot = sisfb_poh_new_node(memheap);
		if(pohRoot == NULL)
			return NULL;

		pohRoot->offset = pohThis->offset;
		pohRoot->size = size;

		pohThis->offset += size;
		pohThis->size -= size;
	}

	memheap->max_freesize -= size;

	pohThis = &memheap->oh_used;
	sisfb_insert_node(pohThis, pohRoot);

	return pohRoot;
}

static void
sisfb_delete_node(struct SIS_OH *poh)
{
	poh->poh_prev->poh_next = poh->poh_next;
	poh->poh_next->poh_prev = poh->poh_prev;
}

static void
sisfb_insert_node(struct SIS_OH *pohList, struct SIS_OH *poh)
{
	struct SIS_OH *pohTemp = pohList->poh_next;

	pohList->poh_next = poh;
	pohTemp->poh_prev = poh;

	poh->poh_prev = pohList;
	poh->poh_next = pohTemp;
}

static struct SIS_OH *
sisfb_poh_free(struct SIS_HEAP *memheap, u32 base)
{
	struct SIS_OH *pohThis;
	struct SIS_OH *poh_freed;
	struct SIS_OH *poh_prev;
	struct SIS_OH *poh_next;
	u32    ulUpper;
	u32    ulLower;
	int    foundNode = 0;

	poh_freed = memheap->oh_used.poh_next;

	while(poh_freed != &memheap->oh_used) {
		if(poh_freed->offset == base) {
			foundNode = 1;
			break;
		}

		poh_freed = poh_freed->poh_next;
	}

	if(!foundNode)
		return NULL;

	memheap->max_freesize += poh_freed->size;

	poh_prev = poh_next = NULL;
	ulUpper = poh_freed->offset + poh_freed->size;
	ulLower = poh_freed->offset;

	pohThis = memheap->oh_free.poh_next;

	while(pohThis != &memheap->oh_free) {
		if(pohThis->offset == ulUpper) {
			poh_next = pohThis;
		} else if((pohThis->offset + pohThis->size) == ulLower) {
			poh_prev = pohThis;
		}
		pohThis = pohThis->poh_next;
	}

	sisfb_delete_node(poh_freed);

	if(poh_prev && poh_next) {
		poh_prev->size += (poh_freed->size + poh_next->size);
		sisfb_delete_node(poh_next);
		sisfb_free_node(memheap, poh_freed);
		sisfb_free_node(memheap, poh_next);
		return poh_prev;
	}

	if(poh_prev) {
		poh_prev->size += poh_freed->size;
		sisfb_free_node(memheap, poh_freed);
		return poh_prev;
	}

	if(poh_next) {
		poh_next->size += poh_freed->size;
		poh_next->offset = poh_freed->offset;
		sisfb_free_node(memheap, poh_freed);
		return poh_next;
	}

	sisfb_insert_node(&memheap->oh_free, poh_freed);

	return poh_freed;
}

static void
sisfb_free_node(struct SIS_HEAP *memheap, struct SIS_OH *poh)
{
	if(poh == NULL)
		return;

	poh->poh_next = memheap->poh_freelist;
	memheap->poh_freelist = poh;
}

static void
sis_int_malloc(struct sis_video_info *ivideo, struct sis_memreq *req)
{
	struct SIS_OH *poh = NULL;

	if((ivideo) && (ivideo->sisfb_id == SISFB_ID) && (!ivideo->havenoheap))
		poh = sisfb_poh_allocate(&ivideo->sisfb_heap, (u32)req->size);

	if(poh == NULL) {
		req->offset = req->size = 0;
		DPRINTK("sisfb: Video RAM allocation failed\n");
	} else {
		req->offset = poh->offset;
		req->size = poh->size;
		DPRINTK("sisfb: Video RAM allocation succeeded: 0x%lx\n",
			(poh->offset + ivideo->video_vbase));
	}
}

void
sis_malloc(struct sis_memreq *req)
{
	struct sis_video_info *ivideo = sisfb_heap->vinfo;

	if(&ivideo->sisfb_heap == sisfb_heap)
		sis_int_malloc(ivideo, req);
	else
		req->offset = req->size = 0;
}

void
sis_malloc_new(struct pci_dev *pdev, struct sis_memreq *req)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);

	sis_int_malloc(ivideo, req);
}

/* sis_free: u32 because "base" is offset inside video ram, can never be >4GB */

static void
sis_int_free(struct sis_video_info *ivideo, u32 base)
{
	struct SIS_OH *poh;

	if((!ivideo) || (ivideo->sisfb_id != SISFB_ID) || (ivideo->havenoheap))
		return;

	poh = sisfb_poh_free(&ivideo->sisfb_heap, base);

	if(poh == NULL) {
		DPRINTK("sisfb: sisfb_poh_free() failed at base 0x%x\n",
			(unsigned int) base);
	}
}

void
sis_free(u32 base)
{
	struct sis_video_info *ivideo = sisfb_heap->vinfo;

	sis_int_free(ivideo, base);
}

void
sis_free_new(struct pci_dev *pdev, u32 base)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);

	sis_int_free(ivideo, base);
}

/* --------------------- SetMode routines ------------------------- */

static void
sisfb_check_engine_and_sync(struct sis_video_info *ivideo)
{
	u8 cr30, cr31;

	/* Check if MMIO and engines are enabled,
	 * and sync in case they are. Can't use
	 * ivideo->accel here, as this might have
	 * been changed before this is called.
	 */
	cr30 = SiS_GetReg(SISSR, IND_SIS_PCI_ADDRESS_SET);
	cr31 = SiS_GetReg(SISSR, IND_SIS_MODULE_ENABLE);
	/* MMIO and 2D/3D engine enabled? */
	if((cr30 & SIS_MEM_MAP_IO_ENABLE) && (cr31 & 0x42)) {
#ifdef CONFIG_FB_SIS_300
		if(ivideo->sisvga_engine == SIS_300_VGA) {
			/* Don't care about TurboQueue. It's
			 * enough to know that the engines
			 * are enabled
			 */
			sisfb_syncaccel(ivideo);
		}
#endif
#ifdef CONFIG_FB_SIS_315
		if(ivideo->sisvga_engine == SIS_315_VGA) {
			/* Check that any queue mode is
			 * enabled, and that the queue
			 * is not in the state of "reset"
			 */
			cr30 = SiS_GetReg(SISSR, 0x26);
			if((cr30 & 0xe0) && (!(cr30 & 0x01))) {
				sisfb_syncaccel(ivideo);
			}
		}
#endif
	}
}

static void
sisfb_pre_setmode(struct sis_video_info *ivideo)
{
	u8 cr30 = 0, cr31 = 0, cr33 = 0, cr35 = 0, cr38 = 0;
	int tvregnum = 0;

	ivideo->currentvbflags &= (VB_VIDEOBRIDGE | VB_DISPTYPE_DISP2);

	SiS_SetReg(SISSR, 0x05, 0x86);

	cr31 = SiS_GetReg(SISCR, 0x31);
	cr31 &= ~0x60;
	cr31 |= 0x04;

	cr33 = ivideo->rate_idx & 0x0F;

#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
	   if(ivideo->chip >= SIS_661) {
	      cr38 = SiS_GetReg(SISCR, 0x38);
	      cr38 &= ~0x07;  /* Clear LCDA/DualEdge and YPbPr bits */
	   } else {
	      tvregnum = 0x38;
	      cr38 = SiS_GetReg(SISCR, tvregnum);
	      cr38 &= ~0x3b;  /* Clear LCDA/DualEdge and YPbPr bits */
	   }
	}
#endif
#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
	   tvregnum = 0x35;
	   cr38 = SiS_GetReg(SISCR, tvregnum);
	}
#endif

	SiS_SetEnableDstn(&ivideo->SiS_Pr, false);
	SiS_SetEnableFstn(&ivideo->SiS_Pr, false);
	ivideo->curFSTN = ivideo->curDSTN = 0;

	switch(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {

	   case CRT2_TV:
	      cr38 &= ~0xc0;   /* Clear PAL-M / PAL-N bits */
	      if((ivideo->vbflags & TV_YPBPR) && (ivideo->vbflags2 & VB2_SISYPBPRBRIDGE)) {
#ifdef CONFIG_FB_SIS_315
		 if(ivideo->chip >= SIS_661) {
		    cr38 |= 0x04;
		    if(ivideo->vbflags & TV_YPBPR525P)       cr35 |= 0x20;
		    else if(ivideo->vbflags & TV_YPBPR750P)  cr35 |= 0x40;
		    else if(ivideo->vbflags & TV_YPBPR1080I) cr35 |= 0x60;
		    cr30 |= SIS_SIMULTANEOUS_VIEW_ENABLE;
		    cr35 &= ~0x01;
		    ivideo->currentvbflags |= (TV_YPBPR | (ivideo->vbflags & TV_YPBPRALL));
		 } else if(ivideo->sisvga_engine == SIS_315_VGA) {
		    cr30 |= (0x80 | SIS_SIMULTANEOUS_VIEW_ENABLE);
		    cr38 |= 0x08;
		    if(ivideo->vbflags & TV_YPBPR525P)       cr38 |= 0x10;
		    else if(ivideo->vbflags & TV_YPBPR750P)  cr38 |= 0x20;
		    else if(ivideo->vbflags & TV_YPBPR1080I) cr38 |= 0x30;
		    cr31 &= ~0x01;
		    ivideo->currentvbflags |= (TV_YPBPR | (ivideo->vbflags & TV_YPBPRALL));
		 }
#endif
	      } else if((ivideo->vbflags & TV_HIVISION) &&
				(ivideo->vbflags2 & VB2_SISHIVISIONBRIDGE)) {
		 if(ivideo->chip >= SIS_661) {
		    cr38 |= 0x04;
		    cr35 |= 0x60;
		 } else {
		    cr30 |= 0x80;
		 }
		 cr30 |= SIS_SIMULTANEOUS_VIEW_ENABLE;
		 cr31 |= 0x01;
		 cr35 |= 0x01;
		 ivideo->currentvbflags |= TV_HIVISION;
	      } else if(ivideo->vbflags & TV_SCART) {
		 cr30 = (SIS_VB_OUTPUT_SCART | SIS_SIMULTANEOUS_VIEW_ENABLE);
		 cr31 |= 0x01;
		 cr35 |= 0x01;
		 ivideo->currentvbflags |= TV_SCART;
	      } else {
		 if(ivideo->vbflags & TV_SVIDEO) {
		    cr30 = (SIS_VB_OUTPUT_SVIDEO | SIS_SIMULTANEOUS_VIEW_ENABLE);
		    ivideo->currentvbflags |= TV_SVIDEO;
		 }
		 if(ivideo->vbflags & TV_AVIDEO) {
		    cr30 = (SIS_VB_OUTPUT_COMPOSITE | SIS_SIMULTANEOUS_VIEW_ENABLE);
		    ivideo->currentvbflags |= TV_AVIDEO;
		 }
	      }
	      cr31 |= SIS_DRIVER_MODE;

	      if(ivideo->vbflags & (TV_AVIDEO | TV_SVIDEO)) {
		 if(ivideo->vbflags & TV_PAL) {
		    cr31 |= 0x01; cr35 |= 0x01;
		    ivideo->currentvbflags |= TV_PAL;
		    if(ivideo->vbflags & TV_PALM) {
		       cr38 |= 0x40; cr35 |= 0x04;
		       ivideo->currentvbflags |= TV_PALM;
		    } else if(ivideo->vbflags & TV_PALN) {
		       cr38 |= 0x80; cr35 |= 0x08;
		       ivideo->currentvbflags |= TV_PALN;
		    }
		 } else {
		    cr31 &= ~0x01; cr35 &= ~0x01;
		    ivideo->currentvbflags |= TV_NTSC;
		    if(ivideo->vbflags & TV_NTSCJ) {
		       cr38 |= 0x40; cr35 |= 0x02;
		       ivideo->currentvbflags |= TV_NTSCJ;
		    }
		 }
	      }
	      break;

	   case CRT2_LCD:
	      cr30  = (SIS_VB_OUTPUT_LCD | SIS_SIMULTANEOUS_VIEW_ENABLE);
	      cr31 |= SIS_DRIVER_MODE;
	      SiS_SetEnableDstn(&ivideo->SiS_Pr, ivideo->sisfb_dstn);
	      SiS_SetEnableFstn(&ivideo->SiS_Pr, ivideo->sisfb_fstn);
	      ivideo->curFSTN = ivideo->sisfb_fstn;
	      ivideo->curDSTN = ivideo->sisfb_dstn;
	      break;

	   case CRT2_VGA:
	      cr30 = (SIS_VB_OUTPUT_CRT2 | SIS_SIMULTANEOUS_VIEW_ENABLE);
	      cr31 |= SIS_DRIVER_MODE;
	      if(ivideo->sisfb_nocrt2rate) {
		 cr33 |= (sisbios_mode[ivideo->sisfb_mode_idx].rate_idx << 4);
	      } else {
		 cr33 |= ((ivideo->rate_idx & 0x0F) << 4);
	      }
	      break;

	   default:	/* disable CRT2 */
	      cr30 = 0x00;
	      cr31 |= (SIS_DRIVER_MODE | SIS_VB_OUTPUT_DISABLE);
	}

	SiS_SetReg(SISCR, 0x30, cr30);
	SiS_SetReg(SISCR, 0x33, cr33);

	if(ivideo->chip >= SIS_661) {
#ifdef CONFIG_FB_SIS_315
	   cr31 &= ~0x01;                          /* Clear PAL flag (now in CR35) */
	   SiS_SetRegANDOR(SISCR, 0x35, ~0x10, cr35); /* Leave overscan bit alone */
	   cr38 &= 0x07;                           /* Use only LCDA and HiVision/YPbPr bits */
	   SiS_SetRegANDOR(SISCR, 0x38, 0xf8, cr38);
#endif
	} else if(ivideo->chip != SIS_300) {
	   SiS_SetReg(SISCR, tvregnum, cr38);
	}
	SiS_SetReg(SISCR, 0x31, cr31);

	ivideo->SiS_Pr.SiS_UseOEM = ivideo->sisfb_useoem;

	sisfb_check_engine_and_sync(ivideo);
}

/* Fix SR11 for 661 and later */
#ifdef CONFIG_FB_SIS_315
static void
sisfb_fixup_SR11(struct sis_video_info *ivideo)
{
	u8  tmpreg;

	if(ivideo->chip >= SIS_661) {
		tmpreg = SiS_GetReg(SISSR, 0x11);
		if(tmpreg & 0x20) {
			tmpreg = SiS_GetReg(SISSR, 0x3e);
			tmpreg = (tmpreg + 1) & 0xff;
			SiS_SetReg(SISSR, 0x3e, tmpreg);
			tmpreg = SiS_GetReg(SISSR, 0x11);
		}
		if(tmpreg & 0xf0) {
			SiS_SetRegAND(SISSR, 0x11, 0x0f);
		}
	}
}
#endif

static void
sisfb_set_TVxposoffset(struct sis_video_info *ivideo, int val)
{
	if(val > 32) val = 32;
	if(val < -32) val = -32;
	ivideo->tvxpos = val;

	if(ivideo->sisfblocked) return;
	if(!ivideo->modechanged) return;

	if(ivideo->currentvbflags & CRT2_TV) {

		if(ivideo->vbflags2 & VB2_CHRONTEL) {

			int x = ivideo->tvx;

			switch(ivideo->chronteltype) {
			case 1:
				x += val;
				if(x < 0) x = 0;
				SiS_SetReg(SISSR, 0x05, 0x86);
				SiS_SetCH700x(&ivideo->SiS_Pr, 0x0a, (x & 0xff));
				SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, 0x08, ((x & 0x0100) >> 7), 0xFD);
				break;
			case 2:
				/* Not supported by hardware */
				break;
			}

		} else if(ivideo->vbflags2 & VB2_SISBRIDGE) {

			u8 p2_1f,p2_20,p2_2b,p2_42,p2_43;
			unsigned short temp;

			p2_1f = ivideo->p2_1f;
			p2_20 = ivideo->p2_20;
			p2_2b = ivideo->p2_2b;
			p2_42 = ivideo->p2_42;
			p2_43 = ivideo->p2_43;

			temp = p2_1f | ((p2_20 & 0xf0) << 4);
			temp += (val * 2);
			p2_1f = temp & 0xff;
			p2_20 = (temp & 0xf00) >> 4;
			p2_2b = ((p2_2b & 0x0f) + (val * 2)) & 0x0f;
			temp = p2_43 | ((p2_42 & 0xf0) << 4);
			temp += (val * 2);
			p2_43 = temp & 0xff;
			p2_42 = (temp & 0xf00) >> 4;
			SiS_SetReg(SISPART2, 0x1f, p2_1f);
			SiS_SetRegANDOR(SISPART2, 0x20, 0x0F, p2_20);
			SiS_SetRegANDOR(SISPART2, 0x2b, 0xF0, p2_2b);
			SiS_SetRegANDOR(SISPART2, 0x42, 0x0F, p2_42);
			SiS_SetReg(SISPART2, 0x43, p2_43);
		}
	}
}

static void
sisfb_set_TVyposoffset(struct sis_video_info *ivideo, int val)
{
	if(val > 32) val = 32;
	if(val < -32) val = -32;
	ivideo->tvypos = val;

	if(ivideo->sisfblocked) return;
	if(!ivideo->modechanged) return;

	if(ivideo->currentvbflags & CRT2_TV) {

		if(ivideo->vbflags2 & VB2_CHRONTEL) {

			int y = ivideo->tvy;

			switch(ivideo->chronteltype) {
			case 1:
				y -= val;
				if(y < 0) y = 0;
				SiS_SetReg(SISSR, 0x05, 0x86);
				SiS_SetCH700x(&ivideo->SiS_Pr, 0x0b, (y & 0xff));
				SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, 0x08, ((y & 0x0100) >> 8), 0xFE);
				break;
			case 2:
				/* Not supported by hardware */
				break;
			}

		} else if(ivideo->vbflags2 & VB2_SISBRIDGE) {

			char p2_01, p2_02;
			val /= 2;
			p2_01 = ivideo->p2_01;
			p2_02 = ivideo->p2_02;

			p2_01 += val;
			p2_02 += val;
			if(!(ivideo->currentvbflags & (TV_HIVISION | TV_YPBPR))) {
				while((p2_01 <= 0) || (p2_02 <= 0)) {
					p2_01 += 2;
					p2_02 += 2;
				}
			}
			SiS_SetReg(SISPART2, 0x01, p2_01);
			SiS_SetReg(SISPART2, 0x02, p2_02);
		}
	}
}

static void
sisfb_post_setmode(struct sis_video_info *ivideo)
{
	bool crt1isoff = false;
	bool doit = true;
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
	u8 reg;
#endif
#ifdef CONFIG_FB_SIS_315
	u8 reg1;
#endif

	SiS_SetReg(SISSR, 0x05, 0x86);

#ifdef CONFIG_FB_SIS_315
	sisfb_fixup_SR11(ivideo);
#endif

	/* Now we actually HAVE changed the display mode */
	ivideo->modechanged = 1;

	/* We can't switch off CRT1 if bridge is in slave mode */
	if(ivideo->vbflags2 & VB2_VIDEOBRIDGE) {
		if(sisfb_bridgeisslave(ivideo)) doit = false;
	} else
		ivideo->sisfb_crt1off = 0;

#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		if((ivideo->sisfb_crt1off) && (doit)) {
			crt1isoff = true;
			reg = 0x00;
		} else {
			crt1isoff = false;
			reg = 0x80;
		}
		SiS_SetRegANDOR(SISCR, 0x17, 0x7f, reg);
	}
#endif
#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
		if((ivideo->sisfb_crt1off) && (doit)) {
			crt1isoff = true;
			reg  = 0x40;
			reg1 = 0xc0;
		} else {
			crt1isoff = false;
			reg  = 0x00;
			reg1 = 0x00;
		}
		SiS_SetRegANDOR(SISCR, ivideo->SiS_Pr.SiS_MyCR63, ~0x40, reg);
		SiS_SetRegANDOR(SISSR, 0x1f, 0x3f, reg1);
	}
#endif

	if(crt1isoff) {
		ivideo->currentvbflags &= ~VB_DISPTYPE_CRT1;
		ivideo->currentvbflags |= VB_SINGLE_MODE;
	} else {
		ivideo->currentvbflags |= VB_DISPTYPE_CRT1;
		if(ivideo->currentvbflags & VB_DISPTYPE_CRT2) {
			ivideo->currentvbflags |= VB_MIRROR_MODE;
		} else {
			ivideo->currentvbflags |= VB_SINGLE_MODE;
		}
	}

	SiS_SetRegAND(SISSR, IND_SIS_RAMDAC_CONTROL, ~0x04);

	if(ivideo->currentvbflags & CRT2_TV) {
		if(ivideo->vbflags2 & VB2_SISBRIDGE) {
			ivideo->p2_1f = SiS_GetReg(SISPART2, 0x1f);
			ivideo->p2_20 = SiS_GetReg(SISPART2, 0x20);
			ivideo->p2_2b = SiS_GetReg(SISPART2, 0x2b);
			ivideo->p2_42 = SiS_GetReg(SISPART2, 0x42);
			ivideo->p2_43 = SiS_GetReg(SISPART2, 0x43);
			ivideo->p2_01 = SiS_GetReg(SISPART2, 0x01);
			ivideo->p2_02 = SiS_GetReg(SISPART2, 0x02);
		} else if(ivideo->vbflags2 & VB2_CHRONTEL) {
			if(ivideo->chronteltype == 1) {
				ivideo->tvx = SiS_GetCH700x(&ivideo->SiS_Pr, 0x0a);
				ivideo->tvx |= (((SiS_GetCH700x(&ivideo->SiS_Pr, 0x08) & 0x02) >> 1) << 8);
				ivideo->tvy = SiS_GetCH700x(&ivideo->SiS_Pr, 0x0b);
				ivideo->tvy |= ((SiS_GetCH700x(&ivideo->SiS_Pr, 0x08) & 0x01) << 8);
			}
		}
	}

	if(ivideo->tvxpos) {
		sisfb_set_TVxposoffset(ivideo, ivideo->tvxpos);
	}
	if(ivideo->tvypos) {
		sisfb_set_TVyposoffset(ivideo, ivideo->tvypos);
	}

	/* Eventually sync engines */
	sisfb_check_engine_and_sync(ivideo);

	/* (Re-)Initialize chip engines */
	if(ivideo->accel) {
		sisfb_engine_init(ivideo);
	} else {
		ivideo->engineok = 0;
	}
}

static int
sisfb_reset_mode(struct sis_video_info *ivideo)
{
	if(sisfb_set_mode(ivideo, 0))
		return 1;

	sisfb_set_pitch(ivideo);
	sisfb_set_base_CRT1(ivideo, ivideo->current_base);
	sisfb_set_base_CRT2(ivideo, ivideo->current_base);

	return 0;
}

static void
sisfb_handle_command(struct sis_video_info *ivideo, struct sisfb_cmd *sisfb_command)
{
	int mycrt1off;

	switch(sisfb_command->sisfb_cmd) {
	case SISFB_CMD_GETVBFLAGS:
		if(!ivideo->modechanged) {
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_EARLY;
		} else {
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_OK;
			sisfb_command->sisfb_result[1] = ivideo->currentvbflags;
			sisfb_command->sisfb_result[2] = ivideo->vbflags2;
		}
		break;
	case SISFB_CMD_SWITCHCRT1:
		/* arg[0]: 0 = off, 1 = on, 99 = query */
		if(!ivideo->modechanged) {
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_EARLY;
		} else if(sisfb_command->sisfb_arg[0] == 99) {
			/* Query */
			sisfb_command->sisfb_result[1] = ivideo->sisfb_crt1off ? 0 : 1;
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_OK;
		} else if(ivideo->sisfblocked) {
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_LOCKED;
		} else if((!(ivideo->currentvbflags & CRT2_ENABLE)) &&
					(sisfb_command->sisfb_arg[0] == 0)) {
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_NOCRT2;
		} else {
			sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_OK;
			mycrt1off = sisfb_command->sisfb_arg[0] ? 0 : 1;
			if( ((ivideo->currentvbflags & VB_DISPTYPE_CRT1) && mycrt1off) ||
			    ((!(ivideo->currentvbflags & VB_DISPTYPE_CRT1)) && !mycrt1off) ) {
				ivideo->sisfb_crt1off = mycrt1off;
				if(sisfb_reset_mode(ivideo)) {
					sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_OTHER;
				}
			}
			sisfb_command->sisfb_result[1] = ivideo->sisfb_crt1off ? 0 : 1;
		}
		break;
	/* more to come */
	default:
		sisfb_command->sisfb_result[0] = SISFB_CMD_ERR_UNKNOWN;
		printk(KERN_ERR "sisfb: Unknown command 0x%x\n",
			sisfb_command->sisfb_cmd);
	}
}

#ifndef MODULE
static int __init sisfb_setup(char *options)
{
	char *this_opt;

	sisfb_setdefaultparms();

	if(!options || !(*options))
		return 0;

	while((this_opt = strsep(&options, ",")) != NULL) {

		if(!(*this_opt)) continue;

		if(!strncasecmp(this_opt, "off", 3)) {
			sisfb_off = 1;
		} else if(!strncasecmp(this_opt, "forcecrt2type:", 14)) {
			/* Need to check crt2 type first for fstn/dstn */
			sisfb_search_crt2type(this_opt + 14);
		} else if(!strncasecmp(this_opt, "tvmode:",7)) {
			sisfb_search_tvstd(this_opt + 7);
		} else if(!strncasecmp(this_opt, "tvstandard:",11)) {
			sisfb_search_tvstd(this_opt + 11);
		} else if(!strncasecmp(this_opt, "mode:", 5)) {
			sisfb_search_mode(this_opt + 5, false);
		} else if(!strncasecmp(this_opt, "vesa:", 5)) {
			sisfb_search_vesamode(simple_strtoul(this_opt + 5, NULL, 0), false);
		} else if(!strncasecmp(this_opt, "rate:", 5)) {
			sisfb_parm_rate = simple_strtoul(this_opt + 5, NULL, 0);
		} else if(!strncasecmp(this_opt, "forcecrt1:", 10)) {
			sisfb_forcecrt1 = (int)simple_strtoul(this_opt + 10, NULL, 0);
		} else if(!strncasecmp(this_opt, "mem:",4)) {
			sisfb_parm_mem = simple_strtoul(this_opt + 4, NULL, 0);
		} else if(!strncasecmp(this_opt, "pdc:", 4)) {
			sisfb_pdc = simple_strtoul(this_opt + 4, NULL, 0);
		} else if(!strncasecmp(this_opt, "pdc1:", 5)) {
			sisfb_pdca = simple_strtoul(this_opt + 5, NULL, 0);
		} else if(!strncasecmp(this_opt, "noaccel", 7)) {
			sisfb_accel = 0;
		} else if(!strncasecmp(this_opt, "accel", 5)) {
			sisfb_accel = -1;
		} else if(!strncasecmp(this_opt, "noypan", 6)) {
			sisfb_ypan = 0;
		} else if(!strncasecmp(this_opt, "ypan", 4)) {
			sisfb_ypan = -1;
		} else if(!strncasecmp(this_opt, "nomax", 5)) {
			sisfb_max = 0;
		} else if(!strncasecmp(this_opt, "max", 3)) {
			sisfb_max = -1;
		} else if(!strncasecmp(this_opt, "userom:", 7)) {
			sisfb_userom = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if(!strncasecmp(this_opt, "useoem:", 7)) {
			sisfb_useoem = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if(!strncasecmp(this_opt, "nocrt2rate", 10)) {
			sisfb_nocrt2rate = 1;
		} else if(!strncasecmp(this_opt, "scalelcd:", 9)) {
			unsigned long temp = 2;
			temp = simple_strtoul(this_opt + 9, NULL, 0);
			if((temp == 0) || (temp == 1)) {
			   sisfb_scalelcd = temp ^ 1;
			}
		} else if(!strncasecmp(this_opt, "tvxposoffset:", 13)) {
			int temp = 0;
			temp = (int)simple_strtol(this_opt + 13, NULL, 0);
			if((temp >= -32) && (temp <= 32)) {
			   sisfb_tvxposoffset = temp;
			}
		} else if(!strncasecmp(this_opt, "tvyposoffset:", 13)) {
			int temp = 0;
			temp = (int)simple_strtol(this_opt + 13, NULL, 0);
			if((temp >= -32) && (temp <= 32)) {
			   sisfb_tvyposoffset = temp;
			}
		} else if(!strncasecmp(this_opt, "specialtiming:", 14)) {
			sisfb_search_specialtiming(this_opt + 14);
		} else if(!strncasecmp(this_opt, "lvdshl:", 7)) {
			int temp = 4;
			temp = simple_strtoul(this_opt + 7, NULL, 0);
			if((temp >= 0) && (temp <= 3)) {
			   sisfb_lvdshl = temp;
			}
		} else if(this_opt[0] >= '0' && this_opt[0] <= '9') {
			sisfb_search_mode(this_opt, true);
#if !defined(__i386__) && !defined(__x86_64__)
		} else if(!strncasecmp(this_opt, "resetcard", 9)) {
			sisfb_resetcard = 1;
	        } else if(!strncasecmp(this_opt, "videoram:", 9)) {
			sisfb_videoram = simple_strtoul(this_opt + 9, NULL, 0);
#endif
		} else {
			printk(KERN_INFO "sisfb: Invalid option %s\n", this_opt);
		}

	}

	return 0;
}
#endif

static int sisfb_check_rom(void __iomem *rom_base,
			   struct sis_video_info *ivideo)
{
	void __iomem *rom;
	int romptr;

	if((readb(rom_base) != 0x55) || (readb(rom_base + 1) != 0xaa))
		return 0;

	romptr = (readb(rom_base + 0x18) | (readb(rom_base + 0x19) << 8));
	if(romptr > (0x10000 - 8))
		return 0;

	rom = rom_base + romptr;

	if((readb(rom)     != 'P') || (readb(rom + 1) != 'C') ||
	   (readb(rom + 2) != 'I') || (readb(rom + 3) != 'R'))
		return 0;

	if((readb(rom + 4) | (readb(rom + 5) << 8)) != ivideo->chip_vendor)
		return 0;

	if((readb(rom + 6) | (readb(rom + 7) << 8)) != ivideo->chip_id)
		return 0;

	return 1;
}

static unsigned char *sisfb_find_rom(struct pci_dev *pdev)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	void __iomem *rom_base;
	unsigned char *myrombase = NULL;
	size_t romsize;

	/* First, try the official pci ROM functions (except
	 * on integrated chipsets which have no ROM).
	 */

	if(!ivideo->nbridge) {

		if((rom_base = pci_map_rom(pdev, &romsize))) {

			if(sisfb_check_rom(rom_base, ivideo)) {

				if((myrombase = vmalloc(65536))) {
					memcpy_fromio(myrombase, rom_base,
							(romsize > 65536) ? 65536 : romsize);
				}
			}
			pci_unmap_rom(pdev, rom_base);
		}
	}

	if(myrombase) return myrombase;

	/* Otherwise do it the conventional way. */

#if defined(__i386__) || defined(__x86_64__)
	{
		u32 temp;

		for (temp = 0x000c0000; temp < 0x000f0000; temp += 0x00001000) {

			rom_base = ioremap(temp, 65536);
			if (!rom_base)
				continue;

			if (!sisfb_check_rom(rom_base, ivideo)) {
				iounmap(rom_base);
				continue;
			}

			if ((myrombase = vmalloc(65536)))
				memcpy_fromio(myrombase, rom_base, 65536);

			iounmap(rom_base);
			break;

		}

	}
#endif

	return myrombase;
}

static void sisfb_post_map_vram(struct sis_video_info *ivideo,
				unsigned int *mapsize, unsigned int min)
{
	if (*mapsize < (min << 20))
		return;

	ivideo->video_vbase = ioremap_wc(ivideo->video_base, (*mapsize));

	if(!ivideo->video_vbase) {
		printk(KERN_ERR
			"sisfb: Unable to map maximum video RAM for size detection\n");
		(*mapsize) >>= 1;
		while((!(ivideo->video_vbase = ioremap_wc(ivideo->video_base, (*mapsize))))) {
			(*mapsize) >>= 1;
			if((*mapsize) < (min << 20))
				break;
		}
		if(ivideo->video_vbase) {
			printk(KERN_ERR
				"sisfb: Video RAM size detection limited to %dMB\n",
				(int)((*mapsize) >> 20));
		}
	}
}

#ifdef CONFIG_FB_SIS_300
static int sisfb_post_300_buswidth(struct sis_video_info *ivideo)
{
	void __iomem *FBAddress = ivideo->video_vbase;
	unsigned short temp;
	unsigned char reg;
	int i, j;

	SiS_SetRegAND(SISSR, 0x15, 0xFB);
	SiS_SetRegOR(SISSR, 0x15, 0x04);
	SiS_SetReg(SISSR, 0x13, 0x00);
	SiS_SetReg(SISSR, 0x14, 0xBF);

	for(i = 0; i < 2; i++) {
		temp = 0x1234;
		for(j = 0; j < 4; j++) {
			writew(temp, FBAddress);
			if(readw(FBAddress) == temp)
				break;
			SiS_SetRegOR(SISSR, 0x3c, 0x01);
			reg = SiS_GetReg(SISSR, 0x05);
			reg = SiS_GetReg(SISSR, 0x05);
			SiS_SetRegAND(SISSR, 0x3c, 0xfe);
			reg = SiS_GetReg(SISSR, 0x05);
			reg = SiS_GetReg(SISSR, 0x05);
			temp++;
		}
	}

	writel(0x01234567L, FBAddress);
	writel(0x456789ABL, (FBAddress + 4));
	writel(0x89ABCDEFL, (FBAddress + 8));
	writel(0xCDEF0123L, (FBAddress + 12));

	reg = SiS_GetReg(SISSR, 0x3b);
	if(reg & 0x01) {
		if(readl((FBAddress + 12)) == 0xCDEF0123L)
			return 4;	/* Channel A 128bit */
	}

	if(readl((FBAddress + 4)) == 0x456789ABL)
		return 2;		/* Channel B 64bit */

	return 1;			/* 32bit */
}

static const unsigned short SiS_DRAMType[17][5] = {
	{0x0C,0x0A,0x02,0x40,0x39},
	{0x0D,0x0A,0x01,0x40,0x48},
	{0x0C,0x09,0x02,0x20,0x35},
	{0x0D,0x09,0x01,0x20,0x44},
	{0x0C,0x08,0x02,0x10,0x31},
	{0x0D,0x08,0x01,0x10,0x40},
	{0x0C,0x0A,0x01,0x20,0x34},
	{0x0C,0x09,0x01,0x08,0x32},
	{0x0B,0x08,0x02,0x08,0x21},
	{0x0C,0x08,0x01,0x08,0x30},
	{0x0A,0x08,0x02,0x04,0x11},
	{0x0B,0x0A,0x01,0x10,0x28},
	{0x09,0x08,0x02,0x02,0x01},
	{0x0B,0x09,0x01,0x08,0x24},
	{0x0B,0x08,0x01,0x04,0x20},
	{0x0A,0x08,0x01,0x02,0x10},
	{0x09,0x08,0x01,0x01,0x00}
};

static int sisfb_post_300_rwtest(struct sis_video_info *ivideo, int iteration,
				 int buswidth, int PseudoRankCapacity,
				 int PseudoAdrPinCount, unsigned int mapsize)
{
	void __iomem *FBAddr = ivideo->video_vbase;
	unsigned short sr14;
	unsigned int k, RankCapacity, PageCapacity, BankNumHigh, BankNumMid;
	unsigned int PhysicalAdrOtherPage, PhysicalAdrHigh, PhysicalAdrHalfPage;

	for (k = 0; k < ARRAY_SIZE(SiS_DRAMType); k++) {
		RankCapacity = buswidth * SiS_DRAMType[k][3];

		if (RankCapacity != PseudoRankCapacity)
			continue;

		if ((SiS_DRAMType[k][2] + SiS_DRAMType[k][0]) > PseudoAdrPinCount)
			continue;

		BankNumHigh = RankCapacity * 16 * iteration - 1;
		if (iteration == 3) {             /* Rank No */
			BankNumMid  = RankCapacity * 16 - 1;
		} else {
			BankNumMid  = RankCapacity * 16 * iteration / 2 - 1;
		}

		PageCapacity = (1 << SiS_DRAMType[k][1]) * buswidth * 4;
		PhysicalAdrHigh = BankNumHigh;
		PhysicalAdrHalfPage = (PageCapacity / 2 + PhysicalAdrHigh) % PageCapacity;
		PhysicalAdrOtherPage = PageCapacity * SiS_DRAMType[k][2] + PhysicalAdrHigh;

		SiS_SetRegAND(SISSR, 0x15, 0xFB); /* Test */
		SiS_SetRegOR(SISSR, 0x15, 0x04);  /* Test */
		sr14 = (SiS_DRAMType[k][3] * buswidth) - 1;

		if (buswidth == 4)
			sr14 |= 0x80;
		else if (buswidth == 2)
			sr14 |= 0x40;

		SiS_SetReg(SISSR, 0x13, SiS_DRAMType[k][4]);
		SiS_SetReg(SISSR, 0x14, sr14);

		BankNumHigh <<= 16;
		BankNumMid <<= 16;

		if ((BankNumHigh + PhysicalAdrHigh >= mapsize) ||
		    (BankNumMid  + PhysicalAdrHigh >= mapsize) ||
		    (BankNumHigh + PhysicalAdrHalfPage  >= mapsize) ||
		    (BankNumHigh + PhysicalAdrOtherPage >= mapsize))
			continue;

		/* Write data */
		writew(((unsigned short)PhysicalAdrHigh),
				(FBAddr + BankNumHigh + PhysicalAdrHigh));
		writew(((unsigned short)BankNumMid),
				(FBAddr + BankNumMid  + PhysicalAdrHigh));
		writew(((unsigned short)PhysicalAdrHalfPage),
				(FBAddr + BankNumHigh + PhysicalAdrHalfPage));
		writew(((unsigned short)PhysicalAdrOtherPage),
				(FBAddr + BankNumHigh + PhysicalAdrOtherPage));

		/* Read data */
		if (readw(FBAddr + BankNumHigh + PhysicalAdrHigh) == PhysicalAdrHigh)
			return 1;
	}

	return 0;
}

static void sisfb_post_300_ramsize(struct pci_dev *pdev, unsigned int mapsize)
{
	struct	sis_video_info *ivideo = pci_get_drvdata(pdev);
	int	i, j, buswidth;
	int	PseudoRankCapacity, PseudoAdrPinCount;

	buswidth = sisfb_post_300_buswidth(ivideo);

	for(i = 6; i >= 0; i--) {
		PseudoRankCapacity = 1 << i;
		for(j = 4; j >= 1; j--) {
			PseudoAdrPinCount = 15 - j;
			if((PseudoRankCapacity * j) <= 64) {
				if(sisfb_post_300_rwtest(ivideo,
						j,
						buswidth,
						PseudoRankCapacity,
						PseudoAdrPinCount,
						mapsize))
					return;
			}
		}
	}
}

static void sisfb_post_sis300(struct pci_dev *pdev)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	unsigned char *bios = ivideo->SiS_Pr.VirtualRomBase;
	u8  reg, v1, v2, v3, v4, v5, v6, v7, v8;
	u16 index, rindex, memtype = 0;
	unsigned int mapsize;

	if(!ivideo->SiS_Pr.UseROM)
		bios = NULL;

	SiS_SetReg(SISSR, 0x05, 0x86);

	if(bios) {
		if(bios[0x52] & 0x80) {
			memtype = bios[0x52];
		} else {
			memtype = SiS_GetReg(SISSR, 0x3a);
		}
		memtype &= 0x07;
	}

	v3 = 0x80; v6 = 0x80;
	if(ivideo->revision_id <= 0x13) {
		v1 = 0x44; v2 = 0x42;
		v4 = 0x44; v5 = 0x42;
	} else {
		v1 = 0x68; v2 = 0x43; /* Assume 125Mhz MCLK */
		v4 = 0x68; v5 = 0x43; /* Assume 125Mhz ECLK */
		if(bios) {
			index = memtype * 5;
			rindex = index + 0x54;
			v1 = bios[rindex++];
			v2 = bios[rindex++];
			v3 = bios[rindex++];
			rindex = index + 0x7c;
			v4 = bios[rindex++];
			v5 = bios[rindex++];
			v6 = bios[rindex++];
		}
	}
	SiS_SetReg(SISSR, 0x28, v1);
	SiS_SetReg(SISSR, 0x29, v2);
	SiS_SetReg(SISSR, 0x2a, v3);
	SiS_SetReg(SISSR, 0x2e, v4);
	SiS_SetReg(SISSR, 0x2f, v5);
	SiS_SetReg(SISSR, 0x30, v6);

	v1 = 0x10;
	if(bios)
		v1 = bios[0xa4];
	SiS_SetReg(SISSR, 0x07, v1);       /* DAC speed */

	SiS_SetReg(SISSR, 0x11, 0x0f);     /* DDC, power save */

	v1 = 0x01; v2 = 0x43; v3 = 0x1e; v4 = 0x2a;
	v5 = 0x06; v6 = 0x00; v7 = 0x00; v8 = 0x00;
	if(bios) {
		memtype += 0xa5;
		v1 = bios[memtype];
		v2 = bios[memtype + 8];
		v3 = bios[memtype + 16];
		v4 = bios[memtype + 24];
		v5 = bios[memtype + 32];
		v6 = bios[memtype + 40];
		v7 = bios[memtype + 48];
		v8 = bios[memtype + 56];
	}
	if(ivideo->revision_id >= 0x80)
		v3 &= 0xfd;
	SiS_SetReg(SISSR, 0x15, v1);       /* Ram type (assuming 0, BIOS 0xa5 step 8) */
	SiS_SetReg(SISSR, 0x16, v2);
	SiS_SetReg(SISSR, 0x17, v3);
	SiS_SetReg(SISSR, 0x18, v4);
	SiS_SetReg(SISSR, 0x19, v5);
	SiS_SetReg(SISSR, 0x1a, v6);
	SiS_SetReg(SISSR, 0x1b, v7);
	SiS_SetReg(SISSR, 0x1c, v8);	   /* ---- */
	SiS_SetRegAND(SISSR, 0x15, 0xfb);
	SiS_SetRegOR(SISSR, 0x15, 0x04);
	if(bios) {
		if(bios[0x53] & 0x02) {
			SiS_SetRegOR(SISSR, 0x19, 0x20);
		}
	}
	v1 = 0x04;			   /* DAC pedestal (BIOS 0xe5) */
	if(ivideo->revision_id >= 0x80)
		v1 |= 0x01;
	SiS_SetReg(SISSR, 0x1f, v1);
	SiS_SetReg(SISSR, 0x20, 0xa4);     /* linear & relocated io & disable a0000 */
	v1 = 0xf6; v2 = 0x0d; v3 = 0x00;
	if(bios) {
		v1 = bios[0xe8];
		v2 = bios[0xe9];
		v3 = bios[0xea];
	}
	SiS_SetReg(SISSR, 0x23, v1);
	SiS_SetReg(SISSR, 0x24, v2);
	SiS_SetReg(SISSR, 0x25, v3);
	SiS_SetReg(SISSR, 0x21, 0x84);
	SiS_SetReg(SISSR, 0x22, 0x00);
	SiS_SetReg(SISCR, 0x37, 0x00);
	SiS_SetRegOR(SISPART1, 0x24, 0x01);   /* unlock crt2 */
	SiS_SetReg(SISPART1, 0x00, 0x00);
	v1 = 0x40; v2 = 0x11;
	if(bios) {
		v1 = bios[0xec];
		v2 = bios[0xeb];
	}
	SiS_SetReg(SISPART1, 0x02, v1);

	if(ivideo->revision_id >= 0x80)
		v2 &= ~0x01;

	reg = SiS_GetReg(SISPART4, 0x00);
	if((reg == 1) || (reg == 2)) {
		SiS_SetReg(SISCR, 0x37, 0x02);
		SiS_SetReg(SISPART2, 0x00, 0x1c);
		v4 = 0x00; v5 = 0x00; v6 = 0x10;
		if (ivideo->SiS_Pr.UseROM && bios) {
			v4 = bios[0xf5];
			v5 = bios[0xf6];
			v6 = bios[0xf7];
		}
		SiS_SetReg(SISPART4, 0x0d, v4);
		SiS_SetReg(SISPART4, 0x0e, v5);
		SiS_SetReg(SISPART4, 0x10, v6);
		SiS_SetReg(SISPART4, 0x0f, 0x3f);
		reg = SiS_GetReg(SISPART4, 0x01);
		if(reg >= 0xb0) {
			reg = SiS_GetReg(SISPART4, 0x23);
			reg &= 0x20;
			reg <<= 1;
			SiS_SetReg(SISPART4, 0x23, reg);
		}
	} else {
		v2 &= ~0x10;
	}
	SiS_SetReg(SISSR, 0x32, v2);

	SiS_SetRegAND(SISPART1, 0x24, 0xfe);  /* Lock CRT2 */

	reg = SiS_GetReg(SISSR, 0x16);
	reg &= 0xc3;
	SiS_SetReg(SISCR, 0x35, reg);
	SiS_SetReg(SISCR, 0x83, 0x00);
#if !defined(__i386__) && !defined(__x86_64__)
	if(sisfb_videoram) {
		SiS_SetReg(SISSR, 0x13, 0x28);  /* ? */
		reg = ((sisfb_videoram >> 10) - 1) | 0x40;
		SiS_SetReg(SISSR, 0x14, reg);
	} else {
#endif
		/* Need to map max FB size for finding out about RAM size */
		mapsize = ivideo->video_size;
		sisfb_post_map_vram(ivideo, &mapsize, 4);

		if(ivideo->video_vbase) {
			sisfb_post_300_ramsize(pdev, mapsize);
			iounmap(ivideo->video_vbase);
		} else {
			printk(KERN_DEBUG
				"sisfb: Failed to map memory for size detection, assuming 8MB\n");
			SiS_SetReg(SISSR, 0x13, 0x28);  /* ? */
			SiS_SetReg(SISSR, 0x14, 0x47);  /* 8MB, 64bit default */
		}
#if !defined(__i386__) && !defined(__x86_64__)
	}
#endif
	if(bios) {
		v1 = bios[0xe6];
		v2 = bios[0xe7];
	} else {
		reg = SiS_GetReg(SISSR, 0x3a);
		if((reg & 0x30) == 0x30) {
			v1 = 0x04; /* PCI */
			v2 = 0x92;
		} else {
			v1 = 0x14; /* AGP */
			v2 = 0xb2;
		}
	}
	SiS_SetReg(SISSR, 0x21, v1);
	SiS_SetReg(SISSR, 0x22, v2);

	/* Sense CRT1 */
	sisfb_sense_crt1(ivideo);

	/* Set default mode, don't clear screen */
	ivideo->SiS_Pr.SiS_UseOEM = false;
	SiS_SetEnableDstn(&ivideo->SiS_Pr, false);
	SiS_SetEnableFstn(&ivideo->SiS_Pr, false);
	ivideo->curFSTN = ivideo->curDSTN = 0;
	ivideo->SiS_Pr.VideoMemorySize = 8 << 20;
	SiSSetMode(&ivideo->SiS_Pr, 0x2e | 0x80);

	SiS_SetReg(SISSR, 0x05, 0x86);

	/* Display off */
	SiS_SetRegOR(SISSR, 0x01, 0x20);

	/* Save mode number in CR34 */
	SiS_SetReg(SISCR, 0x34, 0x2e);

	/* Let everyone know what the current mode is */
	ivideo->modeprechange = 0x2e;
}
#endif

#ifdef CONFIG_FB_SIS_315
#if 0
static void sisfb_post_sis315330(struct pci_dev *pdev)
{
	/* TODO */
}
#endif

static inline int sisfb_xgi_is21(struct sis_video_info *ivideo)
{
	return ivideo->chip_real_id == XGI_21;
}

static void sisfb_post_xgi_delay(struct sis_video_info *ivideo, int delay)
{
	unsigned int i;
	u8 reg;

	for(i = 0; i <= (delay * 10 * 36); i++) {
		reg = SiS_GetReg(SISSR, 0x05);
		reg++;
	}
}

static int sisfb_find_host_bridge(struct sis_video_info *ivideo,
				  struct pci_dev *mypdev,
				  unsigned short pcivendor)
{
	struct pci_dev *pdev = NULL;
	unsigned short temp;
	int ret = 0;

	while((pdev = pci_get_class(PCI_CLASS_BRIDGE_HOST, pdev))) {
		temp = pdev->vendor;
		if(temp == pcivendor) {
			ret = 1;
			pci_dev_put(pdev);
			break;
		}
	}

	return ret;
}

static int sisfb_post_xgi_rwtest(struct sis_video_info *ivideo, int starta,
				 unsigned int enda, unsigned int mapsize)
{
	unsigned int pos;
	int i;

	writel(0, ivideo->video_vbase);

	for(i = starta; i <= enda; i++) {
		pos = 1 << i;
		if(pos < mapsize)
			writel(pos, ivideo->video_vbase + pos);
	}

	sisfb_post_xgi_delay(ivideo, 150);

	if(readl(ivideo->video_vbase) != 0)
		return 0;

	for(i = starta; i <= enda; i++) {
		pos = 1 << i;
		if(pos < mapsize) {
			if(readl(ivideo->video_vbase + pos) != pos)
				return 0;
		} else
			return 0;
	}

	return 1;
}

static int sisfb_post_xgi_ramsize(struct sis_video_info *ivideo)
{
	unsigned int buswidth, ranksize, channelab, mapsize;
	int i, j, k, l, status;
	u8 reg, sr14;
	static const u8 dramsr13[12 * 5] = {
		0x02, 0x0e, 0x0b, 0x80, 0x5d,
		0x02, 0x0e, 0x0a, 0x40, 0x59,
		0x02, 0x0d, 0x0b, 0x40, 0x4d,
		0x02, 0x0e, 0x09, 0x20, 0x55,
		0x02, 0x0d, 0x0a, 0x20, 0x49,
		0x02, 0x0c, 0x0b, 0x20, 0x3d,
		0x02, 0x0e, 0x08, 0x10, 0x51,
		0x02, 0x0d, 0x09, 0x10, 0x45,
		0x02, 0x0c, 0x0a, 0x10, 0x39,
		0x02, 0x0d, 0x08, 0x08, 0x41,
		0x02, 0x0c, 0x09, 0x08, 0x35,
		0x02, 0x0c, 0x08, 0x04, 0x31
	};
	static const u8 dramsr13_4[4 * 5] = {
		0x02, 0x0d, 0x09, 0x40, 0x45,
		0x02, 0x0c, 0x09, 0x20, 0x35,
		0x02, 0x0c, 0x08, 0x10, 0x31,
		0x02, 0x0b, 0x08, 0x08, 0x21
	};

	/* Enable linear mode, disable 0xa0000 address decoding */
	/* We disable a0000 address decoding, because
	 * - if running on x86, if the card is disabled, it means
	 *   that another card is in the system. We don't want
	 *   to interphere with that primary card's textmode.
	 * - if running on non-x86, there usually is no VGA window
	 *   at a0000.
	 */
	SiS_SetRegOR(SISSR, 0x20, (0x80 | 0x04));

	/* Need to map max FB size for finding out about RAM size */
	mapsize = ivideo->video_size;
	sisfb_post_map_vram(ivideo, &mapsize, 32);

	if(!ivideo->video_vbase) {
		printk(KERN_ERR "sisfb: Unable to detect RAM size. Setting default.\n");
		SiS_SetReg(SISSR, 0x13, 0x35);
		SiS_SetReg(SISSR, 0x14, 0x41);
		/* TODO */
		return -ENOMEM;
	}

	/* Non-interleaving */
	SiS_SetReg(SISSR, 0x15, 0x00);
	/* No tiling */
	SiS_SetReg(SISSR, 0x1c, 0x00);

	if(ivideo->chip == XGI_20) {

		channelab = 1;
		reg = SiS_GetReg(SISCR, 0x97);
		if(!(reg & 0x01)) {	/* Single 32/16 */
			buswidth = 32;
			SiS_SetReg(SISSR, 0x13, 0xb1);
			SiS_SetReg(SISSR, 0x14, 0x52);
			sisfb_post_xgi_delay(ivideo, 1);
			sr14 = 0x02;
			if(sisfb_post_xgi_rwtest(ivideo, 23, 24, mapsize))
				goto bail_out;

			SiS_SetReg(SISSR, 0x13, 0x31);
			SiS_SetReg(SISSR, 0x14, 0x42);
			sisfb_post_xgi_delay(ivideo, 1);
			if(sisfb_post_xgi_rwtest(ivideo, 23, 23, mapsize))
				goto bail_out;

			buswidth = 16;
			SiS_SetReg(SISSR, 0x13, 0xb1);
			SiS_SetReg(SISSR, 0x14, 0x41);
			sisfb_post_xgi_delay(ivideo, 1);
			sr14 = 0x01;
			if(sisfb_post_xgi_rwtest(ivideo, 22, 23, mapsize))
				goto bail_out;
			else
				SiS_SetReg(SISSR, 0x13, 0x31);
		} else {		/* Dual 16/8 */
			buswidth = 16;
			SiS_SetReg(SISSR, 0x13, 0xb1);
			SiS_SetReg(SISSR, 0x14, 0x41);
			sisfb_post_xgi_delay(ivideo, 1);
			sr14 = 0x01;
			if(sisfb_post_xgi_rwtest(ivideo, 22, 23, mapsize))
				goto bail_out;

			SiS_SetReg(SISSR, 0x13, 0x31);
			SiS_SetReg(SISSR, 0x14, 0x31);
			sisfb_post_xgi_delay(ivideo, 1);
			if(sisfb_post_xgi_rwtest(ivideo, 22, 22, mapsize))
				goto bail_out;

			buswidth = 8;
			SiS_SetReg(SISSR, 0x13, 0xb1);
			SiS_SetReg(SISSR, 0x14, 0x30);
			sisfb_post_xgi_delay(ivideo, 1);
			sr14 = 0x00;
			if(sisfb_post_xgi_rwtest(ivideo, 21, 22, mapsize))
				goto bail_out;
			else
				SiS_SetReg(SISSR, 0x13, 0x31);
		}

	} else {	/* XGI_40 */

		reg = SiS_GetReg(SISCR, 0x97);
		if(!(reg & 0x10)) {
			reg = SiS_GetReg(SISSR, 0x39);
			reg >>= 1;
		}

		if(reg & 0x01) {	/* DDRII */
			buswidth = 32;
			if(ivideo->revision_id == 2) {
				channelab = 2;
				SiS_SetReg(SISSR, 0x13, 0xa1);
				SiS_SetReg(SISSR, 0x14, 0x44);
				sr14 = 0x04;
				sisfb_post_xgi_delay(ivideo, 1);
				if(sisfb_post_xgi_rwtest(ivideo, 23, 24, mapsize))
					goto bail_out;

				SiS_SetReg(SISSR, 0x13, 0x21);
				SiS_SetReg(SISSR, 0x14, 0x34);
				if(sisfb_post_xgi_rwtest(ivideo, 22, 23, mapsize))
					goto bail_out;

				channelab = 1;
				SiS_SetReg(SISSR, 0x13, 0xa1);
				SiS_SetReg(SISSR, 0x14, 0x40);
				sr14 = 0x00;
				if(sisfb_post_xgi_rwtest(ivideo, 22, 23, mapsize))
					goto bail_out;

				SiS_SetReg(SISSR, 0x13, 0x21);
				SiS_SetReg(SISSR, 0x14, 0x30);
			} else {
				channelab = 3;
				SiS_SetReg(SISSR, 0x13, 0xa1);
				SiS_SetReg(SISSR, 0x14, 0x4c);
				sr14 = 0x0c;
				sisfb_post_xgi_delay(ivideo, 1);
				if(sisfb_post_xgi_rwtest(ivideo, 23, 25, mapsize))
					goto bail_out;

				channelab = 2;
				SiS_SetReg(SISSR, 0x14, 0x48);
				sisfb_post_xgi_delay(ivideo, 1);
				sr14 = 0x08;
				if(sisfb_post_xgi_rwtest(ivideo, 23, 24, mapsize))
					goto bail_out;

				SiS_SetReg(SISSR, 0x13, 0x21);
				SiS_SetReg(SISSR, 0x14, 0x3c);
				sr14 = 0x0c;

				if(sisfb_post_xgi_rwtest(ivideo, 23, 24, mapsize)) {
					channelab = 3;
				} else {
					channelab = 2;
					SiS_SetReg(SISSR, 0x14, 0x38);
					sr14 = 0x08;
				}
			}
			sisfb_post_xgi_delay(ivideo, 1);

		} else {	/* DDR */

			buswidth = 64;
			if(ivideo->revision_id == 2) {
				channelab = 1;
				SiS_SetReg(SISSR, 0x13, 0xa1);
				SiS_SetReg(SISSR, 0x14, 0x52);
				sisfb_post_xgi_delay(ivideo, 1);
				sr14 = 0x02;
				if(sisfb_post_xgi_rwtest(ivideo, 23, 24, mapsize))
					goto bail_out;

				SiS_SetReg(SISSR, 0x13, 0x21);
				SiS_SetReg(SISSR, 0x14, 0x42);
			} else {
				channelab = 2;
				SiS_SetReg(SISSR, 0x13, 0xa1);
				SiS_SetReg(SISSR, 0x14, 0x5a);
				sisfb_post_xgi_delay(ivideo, 1);
				sr14 = 0x0a;
				if(sisfb_post_xgi_rwtest(ivideo, 24, 25, mapsize))
					goto bail_out;

				SiS_SetReg(SISSR, 0x13, 0x21);
				SiS_SetReg(SISSR, 0x14, 0x4a);
			}
			sisfb_post_xgi_delay(ivideo, 1);

		}
	}

bail_out:
	SiS_SetRegANDOR(SISSR, 0x14, 0xf0, sr14);
	sisfb_post_xgi_delay(ivideo, 1);

	j = (ivideo->chip == XGI_20) ? 5 : 9;
	k = (ivideo->chip == XGI_20) ? 12 : 4;
	status = -EIO;

	for(i = 0; i < k; i++) {

		reg = (ivideo->chip == XGI_20) ?
				dramsr13[(i * 5) + 4] : dramsr13_4[(i * 5) + 4];
		SiS_SetRegANDOR(SISSR, 0x13, 0x80, reg);
		sisfb_post_xgi_delay(ivideo, 50);

		ranksize = (ivideo->chip == XGI_20) ?
				dramsr13[(i * 5) + 3] : dramsr13_4[(i * 5) + 3];

		reg = SiS_GetReg(SISSR, 0x13);
		if(reg & 0x80) ranksize <<= 1;

		if(ivideo->chip == XGI_20) {
			if(buswidth == 16)      ranksize <<= 1;
			else if(buswidth == 32) ranksize <<= 2;
		} else {
			if(buswidth == 64)      ranksize <<= 1;
		}

		reg = 0;
		l = channelab;
		if(l == 3) l = 4;
		if((ranksize * l) <= 256) {
			while((ranksize >>= 1)) reg += 0x10;
		}

		if(!reg) continue;

		SiS_SetRegANDOR(SISSR, 0x14, 0x0f, (reg & 0xf0));
		sisfb_post_xgi_delay(ivideo, 1);

		if (sisfb_post_xgi_rwtest(ivideo, j, ((reg >> 4) + channelab - 2 + 20), mapsize)) {
			status = 0;
			break;
		}
	}

	iounmap(ivideo->video_vbase);

	return status;
}

static void sisfb_post_xgi_setclocks(struct sis_video_info *ivideo, u8 regb)
{
	u8 v1, v2, v3;
	int index;
	static const u8 cs90[8 * 3] = {
		0x16, 0x01, 0x01,
		0x3e, 0x03, 0x01,
		0x7c, 0x08, 0x01,
		0x79, 0x06, 0x01,
		0x29, 0x01, 0x81,
		0x5c, 0x23, 0x01,
		0x5c, 0x23, 0x01,
		0x5c, 0x23, 0x01
	};
	static const u8 csb8[8 * 3] = {
		0x5c, 0x23, 0x01,
		0x29, 0x01, 0x01,
		0x7c, 0x08, 0x01,
		0x79, 0x06, 0x01,
		0x29, 0x01, 0x81,
		0x5c, 0x23, 0x01,
		0x5c, 0x23, 0x01,
		0x5c, 0x23, 0x01
	};

	regb = 0;  /* ! */

	index = regb * 3;
	v1 = cs90[index]; v2 = cs90[index + 1]; v3 = cs90[index + 2];
	if(ivideo->haveXGIROM) {
		v1 = ivideo->bios_abase[0x90 + index];
		v2 = ivideo->bios_abase[0x90 + index + 1];
		v3 = ivideo->bios_abase[0x90 + index + 2];
	}
	SiS_SetReg(SISSR, 0x28, v1);
	SiS_SetReg(SISSR, 0x29, v2);
	SiS_SetReg(SISSR, 0x2a, v3);
	sisfb_post_xgi_delay(ivideo, 0x43);
	sisfb_post_xgi_delay(ivideo, 0x43);
	sisfb_post_xgi_delay(ivideo, 0x43);
	index = regb * 3;
	v1 = csb8[index]; v2 = csb8[index + 1]; v3 = csb8[index + 2];
	if(ivideo->haveXGIROM) {
		v1 = ivideo->bios_abase[0xb8 + index];
		v2 = ivideo->bios_abase[0xb8 + index + 1];
		v3 = ivideo->bios_abase[0xb8 + index + 2];
	}
	SiS_SetReg(SISSR, 0x2e, v1);
	SiS_SetReg(SISSR, 0x2f, v2);
	SiS_SetReg(SISSR, 0x30, v3);
	sisfb_post_xgi_delay(ivideo, 0x43);
	sisfb_post_xgi_delay(ivideo, 0x43);
	sisfb_post_xgi_delay(ivideo, 0x43);
}

static void sisfb_post_xgi_ddr2_mrs_default(struct sis_video_info *ivideo,
					    u8 regb)
{
	unsigned char *bios = ivideo->bios_abase;
	u8 v1;

	SiS_SetReg(SISSR, 0x28, 0x64);
	SiS_SetReg(SISSR, 0x29, 0x63);
	sisfb_post_xgi_delay(ivideo, 15);
	SiS_SetReg(SISSR, 0x18, 0x00);
	SiS_SetReg(SISSR, 0x19, 0x20);
	SiS_SetReg(SISSR, 0x16, 0x00);
	SiS_SetReg(SISSR, 0x16, 0x80);
	SiS_SetReg(SISSR, 0x18, 0xc5);
	SiS_SetReg(SISSR, 0x19, 0x23);
	SiS_SetReg(SISSR, 0x16, 0x00);
	SiS_SetReg(SISSR, 0x16, 0x80);
	sisfb_post_xgi_delay(ivideo, 1);
	SiS_SetReg(SISCR, 0x97, 0x11);
	sisfb_post_xgi_setclocks(ivideo, regb);
	sisfb_post_xgi_delay(ivideo, 0x46);
	SiS_SetReg(SISSR, 0x18, 0xc5);
	SiS_SetReg(SISSR, 0x19, 0x23);
	SiS_SetReg(SISSR, 0x16, 0x00);
	SiS_SetReg(SISSR, 0x16, 0x80);
	sisfb_post_xgi_delay(ivideo, 1);
	SiS_SetReg(SISSR, 0x1b, 0x04);
	sisfb_post_xgi_delay(ivideo, 1);
	SiS_SetReg(SISSR, 0x1b, 0x00);
	sisfb_post_xgi_delay(ivideo, 1);
	v1 = 0x31;
	if (ivideo->haveXGIROM) {
		v1 = bios[0xf0];
	}
	SiS_SetReg(SISSR, 0x18, v1);
	SiS_SetReg(SISSR, 0x19, 0x06);
	SiS_SetReg(SISSR, 0x16, 0x04);
	SiS_SetReg(SISSR, 0x16, 0x84);
	sisfb_post_xgi_delay(ivideo, 1);
}

static void sisfb_post_xgi_ddr2_mrs_xg21(struct sis_video_info *ivideo)
{
	sisfb_post_xgi_setclocks(ivideo, 1);

	SiS_SetReg(SISCR, 0x97, 0x11);
	sisfb_post_xgi_delay(ivideo, 0x46);

	SiS_SetReg(SISSR, 0x18, 0x00);	/* EMRS2 */
	SiS_SetReg(SISSR, 0x19, 0x80);
	SiS_SetReg(SISSR, 0x16, 0x05);
	SiS_SetReg(SISSR, 0x16, 0x85);

	SiS_SetReg(SISSR, 0x18, 0x00);	/* EMRS3 */
	SiS_SetReg(SISSR, 0x19, 0xc0);
	SiS_SetReg(SISSR, 0x16, 0x05);
	SiS_SetReg(SISSR, 0x16, 0x85);

	SiS_SetReg(SISSR, 0x18, 0x00);	/* EMRS1 */
	SiS_SetReg(SISSR, 0x19, 0x40);
	SiS_SetReg(SISSR, 0x16, 0x05);
	SiS_SetReg(SISSR, 0x16, 0x85);

	SiS_SetReg(SISSR, 0x18, 0x42);	/* MRS1 */
	SiS_SetReg(SISSR, 0x19, 0x02);
	SiS_SetReg(SISSR, 0x16, 0x05);
	SiS_SetReg(SISSR, 0x16, 0x85);
	sisfb_post_xgi_delay(ivideo, 1);

	SiS_SetReg(SISSR, 0x1b, 0x04);
	sisfb_post_xgi_delay(ivideo, 1);

	SiS_SetReg(SISSR, 0x1b, 0x00);
	sisfb_post_xgi_delay(ivideo, 1);

	SiS_SetReg(SISSR, 0x18, 0x42);	/* MRS1 */
	SiS_SetReg(SISSR, 0x19, 0x00);
	SiS_SetReg(SISSR, 0x16, 0x05);
	SiS_SetReg(SISSR, 0x16, 0x85);
	sisfb_post_xgi_delay(ivideo, 1);
}

static void sisfb_post_xgi_ddr2(struct sis_video_info *ivideo, u8 regb)
{
	unsigned char *bios = ivideo->bios_abase;
	static const u8 cs158[8] = {
		0x88, 0xaa, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs160[8] = {
		0x44, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs168[8] = {
		0x48, 0x78, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	u8 v1;
	u8 v2;
	u8 v3;

	SiS_SetReg(SISCR, 0xb0, 0x80); /* DDR2 dual frequency mode */
	SiS_SetReg(SISCR, 0x82, 0x77);
	SiS_SetReg(SISCR, 0x86, 0x00);
	SiS_GetReg(SISCR, 0x86);
	SiS_SetReg(SISCR, 0x86, 0x88);
	SiS_GetReg(SISCR, 0x86);
	v1 = cs168[regb]; v2 = cs160[regb]; v3 = cs158[regb];
	if (ivideo->haveXGIROM) {
		v1 = bios[regb + 0x168];
		v2 = bios[regb + 0x160];
		v3 = bios[regb + 0x158];
	}
	SiS_SetReg(SISCR, 0x86, v1);
	SiS_SetReg(SISCR, 0x82, 0x77);
	SiS_SetReg(SISCR, 0x85, 0x00);
	SiS_GetReg(SISCR, 0x85);
	SiS_SetReg(SISCR, 0x85, 0x88);
	SiS_GetReg(SISCR, 0x85);
	SiS_SetReg(SISCR, 0x85, v2);
	SiS_SetReg(SISCR, 0x82, v3);
	SiS_SetReg(SISCR, 0x98, 0x01);
	SiS_SetReg(SISCR, 0x9a, 0x02);
	if (sisfb_xgi_is21(ivideo))
		sisfb_post_xgi_ddr2_mrs_xg21(ivideo);
	else
		sisfb_post_xgi_ddr2_mrs_default(ivideo, regb);
}

static u8 sisfb_post_xgi_ramtype(struct sis_video_info *ivideo)
{
	unsigned char *bios = ivideo->bios_abase;
	u8 ramtype;
	u8 reg;
	u8 v1;

	ramtype = 0x00; v1 = 0x10;
	if (ivideo->haveXGIROM) {
		ramtype = bios[0x62];
		v1 = bios[0x1d2];
	}
	if (!(ramtype & 0x80)) {
		if (sisfb_xgi_is21(ivideo)) {
			SiS_SetRegAND(SISCR, 0xb4, 0xfd); /* GPIO control */
			SiS_SetRegOR(SISCR, 0x4a, 0x80);  /* GPIOH EN */
			reg = SiS_GetReg(SISCR, 0x48);
			SiS_SetRegOR(SISCR, 0xb4, 0x02);
			ramtype = reg & 0x01;		  /* GPIOH */
		} else if (ivideo->chip == XGI_20) {
			SiS_SetReg(SISCR, 0x97, v1);
			reg = SiS_GetReg(SISCR, 0x97);
			if (reg & 0x10) {
				ramtype = (reg & 0x01) << 1;
			}
		} else {
			reg = SiS_GetReg(SISSR, 0x39);
			ramtype = reg & 0x02;
			if (!(ramtype)) {
				reg = SiS_GetReg(SISSR, 0x3a);
				ramtype = (reg >> 1) & 0x01;
			}
		}
	}
	ramtype &= 0x07;

	return ramtype;
}

static int sisfb_post_xgi(struct pci_dev *pdev)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	unsigned char *bios = ivideo->bios_abase;
	struct pci_dev *mypdev = NULL;
	const u8 *ptr, *ptr2;
	u8 v1, v2, v3, v4, v5, reg, ramtype;
	u32 rega, regb, regd;
	int i, j, k, index;
	static const u8 cs78[3] = { 0xf6, 0x0d, 0x00 };
	static const u8 cs76[2] = { 0xa3, 0xfb };
	static const u8 cs7b[3] = { 0xc0, 0x11, 0x00 };
	static const u8 cs158[8] = {
		0x88, 0xaa, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs160[8] = {
		0x44, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs168[8] = {
		0x48, 0x78, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs128[3 * 8] = {
		0x90, 0x28, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x77, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x77, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs148[2 * 8] = {
		0x55, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs31a[8 * 4] = {
		0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
		0xaa, 0xaa, 0xaa, 0xaa, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs33a[8 * 4] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs45a[8 * 2] = {
		0x00, 0x00, 0xa0, 0x00, 0xa0, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs170[7 * 8] = {
		0x54, 0x32, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x54, 0x43, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x0a, 0x05, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x44, 0x34, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x11, 0x0c, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x05, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs1a8[3 * 8] = {
		0xf0, 0xf0, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x05, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const u8 cs100[2 * 8] = {
		0xc4, 0x04, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xc4, 0x04, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	/* VGA enable */
	reg = SiS_GetRegByte(SISVGAENABLE) | 0x01;
	SiS_SetRegByte(SISVGAENABLE, reg);

	/* Misc */
	reg = SiS_GetRegByte(SISMISCR) | 0x01;
	SiS_SetRegByte(SISMISCW, reg);

	/* Unlock SR */
	SiS_SetReg(SISSR, 0x05, 0x86);
	reg = SiS_GetReg(SISSR, 0x05);
	if(reg != 0xa1)
		return 0;

	/* Clear some regs */
	for(i = 0; i < 0x22; i++) {
		if(0x06 + i == 0x20) continue;
		SiS_SetReg(SISSR, 0x06 + i, 0x00);
	}
	for(i = 0; i < 0x0b; i++) {
		SiS_SetReg(SISSR, 0x31 + i, 0x00);
	}
	for(i = 0; i < 0x10; i++) {
		SiS_SetReg(SISCR, 0x30 + i, 0x00);
	}

	ptr = cs78;
	if(ivideo->haveXGIROM) {
		ptr = (const u8 *)&bios[0x78];
	}
	for(i = 0; i < 3; i++) {
		SiS_SetReg(SISSR, 0x23 + i, ptr[i]);
	}

	ptr = cs76;
	if(ivideo->haveXGIROM) {
		ptr = (const u8 *)&bios[0x76];
	}
	for(i = 0; i < 2; i++) {
		SiS_SetReg(SISSR, 0x21 + i, ptr[i]);
	}

	v1 = 0x18; v2 = 0x00;
	if(ivideo->haveXGIROM) {
		v1 = bios[0x74];
		v2 = bios[0x75];
	}
	SiS_SetReg(SISSR, 0x07, v1);
	SiS_SetReg(SISSR, 0x11, 0x0f);
	SiS_SetReg(SISSR, 0x1f, v2);
	/* PCI linear mode, RelIO enabled, A0000 decoding disabled */
	SiS_SetReg(SISSR, 0x20, 0x80 | 0x20 | 0x04);
	SiS_SetReg(SISSR, 0x27, 0x74);

	ptr = cs7b;
	if(ivideo->haveXGIROM) {
		ptr = (const u8 *)&bios[0x7b];
	}
	for(i = 0; i < 3; i++) {
		SiS_SetReg(SISSR, 0x31 + i, ptr[i]);
	}

	if(ivideo->chip == XGI_40) {
		if(ivideo->revision_id == 2) {
			SiS_SetRegANDOR(SISSR, 0x3b, 0x3f, 0xc0);
		}
		SiS_SetReg(SISCR, 0x7d, 0xfe);
		SiS_SetReg(SISCR, 0x7e, 0x0f);
	}
	if(ivideo->revision_id == 0) {	/* 40 *and* 20? */
		SiS_SetRegAND(SISCR, 0x58, 0xd7);
		reg = SiS_GetReg(SISCR, 0xcb);
		if(reg & 0x20) {
			SiS_SetRegANDOR(SISCR, 0x58, 0xd7, (reg & 0x10) ? 0x08 : 0x20); /* =0x28 Z7 ? */
		}
	}

	reg = (ivideo->chip == XGI_40) ? 0x20 : 0x00;
	SiS_SetRegANDOR(SISCR, 0x38, 0x1f, reg);

	if(ivideo->chip == XGI_20) {
		SiS_SetReg(SISSR, 0x36, 0x70);
	} else {
		SiS_SetReg(SISVID, 0x00, 0x86);
		SiS_SetReg(SISVID, 0x32, 0x00);
		SiS_SetReg(SISVID, 0x30, 0x00);
		SiS_SetReg(SISVID, 0x32, 0x01);
		SiS_SetReg(SISVID, 0x30, 0x00);
		SiS_SetRegAND(SISVID, 0x2f, 0xdf);
		SiS_SetRegAND(SISCAP, 0x00, 0x3f);

		SiS_SetReg(SISPART1, 0x2f, 0x01);
		SiS_SetReg(SISPART1, 0x00, 0x00);
		SiS_SetReg(SISPART1, 0x02, bios[0x7e]);
		SiS_SetReg(SISPART1, 0x2e, 0x08);
		SiS_SetRegAND(SISPART1, 0x35, 0x7f);
		SiS_SetRegAND(SISPART1, 0x50, 0xfe);

		reg = SiS_GetReg(SISPART4, 0x00);
		if(reg == 1 || reg == 2) {
			SiS_SetReg(SISPART2, 0x00, 0x1c);
			SiS_SetReg(SISPART4, 0x0d, bios[0x7f]);
			SiS_SetReg(SISPART4, 0x0e, bios[0x80]);
			SiS_SetReg(SISPART4, 0x10, bios[0x81]);
			SiS_SetRegAND(SISPART4, 0x0f, 0x3f);

			reg = SiS_GetReg(SISPART4, 0x01);
			if((reg & 0xf0) >= 0xb0) {
				reg = SiS_GetReg(SISPART4, 0x23);
				if(reg & 0x20) reg |= 0x40;
				SiS_SetReg(SISPART4, 0x23, reg);
				reg = (reg & 0x20) ? 0x02 : 0x00;
				SiS_SetRegANDOR(SISPART1, 0x1e, 0xfd, reg);
			}
		}

		v1 = bios[0x77];

		reg = SiS_GetReg(SISSR, 0x3b);
		if(reg & 0x02) {
			reg = SiS_GetReg(SISSR, 0x3a);
			v2 = (reg & 0x30) >> 3;
			if(!(v2 & 0x04)) v2 ^= 0x02;
			reg = SiS_GetReg(SISSR, 0x39);
			if(reg & 0x80) v2 |= 0x80;
			v2 |= 0x01;

			if((mypdev = pci_get_device(PCI_VENDOR_ID_SI, 0x0730, NULL))) {
				pci_dev_put(mypdev);
				if(((v2 & 0x06) == 2) || ((v2 & 0x06) == 4))
					v2 &= 0xf9;
				v2 |= 0x08;
				v1 &= 0xfe;
			} else {
				mypdev = pci_get_device(PCI_VENDOR_ID_SI, 0x0735, NULL);
				if(!mypdev)
					mypdev = pci_get_device(PCI_VENDOR_ID_SI, 0x0645, NULL);
				if(!mypdev)
					mypdev = pci_get_device(PCI_VENDOR_ID_SI, 0x0650, NULL);
				if(mypdev) {
					pci_read_config_dword(mypdev, 0x94, &regd);
					regd &= 0xfffffeff;
					pci_write_config_dword(mypdev, 0x94, regd);
					v1 &= 0xfe;
					pci_dev_put(mypdev);
				} else if(sisfb_find_host_bridge(ivideo, pdev, PCI_VENDOR_ID_SI)) {
					v1 &= 0xfe;
				} else if(sisfb_find_host_bridge(ivideo, pdev, 0x1106) ||
					  sisfb_find_host_bridge(ivideo, pdev, 0x1022) ||
					  sisfb_find_host_bridge(ivideo, pdev, 0x700e) ||
					  sisfb_find_host_bridge(ivideo, pdev, 0x10de)) {
					if((v2 & 0x06) == 4)
						v2 ^= 0x06;
					v2 |= 0x08;
				}
			}
			SiS_SetRegANDOR(SISCR, 0x5f, 0xf0, v2);
		}
		SiS_SetReg(SISSR, 0x22, v1);

		if(ivideo->revision_id == 2) {
			v1 = SiS_GetReg(SISSR, 0x3b);
			v2 = SiS_GetReg(SISSR, 0x3a);
			regd = bios[0x90 + 3] | (bios[0x90 + 4] << 8);
			if( (!(v1 & 0x02)) && (v2 & 0x30) && (regd < 0xcf) )
				SiS_SetRegANDOR(SISCR, 0x5f, 0xf1, 0x01);

			if((mypdev = pci_get_device(0x10de, 0x01e0, NULL))) {
				/* TODO: set CR5f &0xf1 | 0x01 for version 6570
				 * of nforce 2 ROM
				 */
				if(0)
					SiS_SetRegANDOR(SISCR, 0x5f, 0xf1, 0x01);
				pci_dev_put(mypdev);
			}
		}

		v1 = 0x30;
		reg = SiS_GetReg(SISSR, 0x3b);
		v2 = SiS_GetReg(SISCR, 0x5f);
		if((!(reg & 0x02)) && (v2 & 0x0e))
			v1 |= 0x08;
		SiS_SetReg(SISSR, 0x27, v1);

		if(bios[0x64] & 0x01) {
			SiS_SetRegANDOR(SISCR, 0x5f, 0xf0, bios[0x64]);
		}

		v1 = bios[0x4f7];
		pci_read_config_dword(pdev, 0x50, &regd);
		regd = (regd >> 20) & 0x0f;
		if(regd == 1) {
			v1 &= 0xfc;
			SiS_SetRegOR(SISCR, 0x5f, 0x08);
		}
		SiS_SetReg(SISCR, 0x48, v1);

		SiS_SetRegANDOR(SISCR, 0x47, 0x04, bios[0x4f6] & 0xfb);
		SiS_SetRegANDOR(SISCR, 0x49, 0xf0, bios[0x4f8] & 0x0f);
		SiS_SetRegANDOR(SISCR, 0x4a, 0x60, bios[0x4f9] & 0x9f);
		SiS_SetRegANDOR(SISCR, 0x4b, 0x08, bios[0x4fa] & 0xf7);
		SiS_SetRegANDOR(SISCR, 0x4c, 0x80, bios[0x4fb] & 0x7f);
		SiS_SetReg(SISCR, 0x70, bios[0x4fc]);
		SiS_SetRegANDOR(SISCR, 0x71, 0xf0, bios[0x4fd] & 0x0f);
		SiS_SetReg(SISCR, 0x74, 0xd0);
		SiS_SetRegANDOR(SISCR, 0x74, 0xcf, bios[0x4fe] & 0x30);
		SiS_SetRegANDOR(SISCR, 0x75, 0xe0, bios[0x4ff] & 0x1f);
		SiS_SetRegANDOR(SISCR, 0x76, 0xe0, bios[0x500] & 0x1f);
		v1 = bios[0x501];
		if((mypdev = pci_get_device(0x8086, 0x2530, NULL))) {
			v1 = 0xf0;
			pci_dev_put(mypdev);
		}
		SiS_SetReg(SISCR, 0x77, v1);
	}

	/* RAM type:
	 *
	 * 0 == DDR1, 1 == DDR2, 2..7 == reserved?
	 *
	 * The code seems to written so that regb should equal ramtype,
	 * however, so far it has been hardcoded to 0. Enable other values only
	 * on XGI Z9, as it passes the POST, and add a warning for others.
	 */
	ramtype = sisfb_post_xgi_ramtype(ivideo);
	if (!sisfb_xgi_is21(ivideo) && ramtype) {
		dev_warn(&pdev->dev,
			 "RAM type something else than expected: %d\n",
			 ramtype);
		regb = 0;
	} else {
		regb = ramtype;
	}

	v1 = 0xff;
	if(ivideo->haveXGIROM) {
		v1 = bios[0x140 + regb];
	}
	SiS_SetReg(SISCR, 0x6d, v1);

	ptr = cs128;
	if(ivideo->haveXGIROM) {
		ptr = (const u8 *)&bios[0x128];
	}
	for(i = 0, j = 0; i < 3; i++, j += 8) {
		SiS_SetReg(SISCR, 0x68 + i, ptr[j + regb]);
	}

	ptr  = cs31a;
	ptr2 = cs33a;
	if(ivideo->haveXGIROM) {
		index = (ivideo->chip == XGI_20) ? 0x31a : 0x3a6;
		ptr  = (const u8 *)&bios[index];
		ptr2 = (const u8 *)&bios[index + 0x20];
	}
	for(i = 0; i < 2; i++) {
		if(i == 0) {
			regd = le32_to_cpu(((u32 *)ptr)[regb]);
			rega = 0x6b;
		} else {
			regd = le32_to_cpu(((u32 *)ptr2)[regb]);
			rega = 0x6e;
		}
		reg = 0x00;
		for(j = 0; j < 16; j++) {
			reg &= 0xf3;
			if(regd & 0x01) reg |= 0x04;
			if(regd & 0x02) reg |= 0x08;
			regd >>= 2;
			SiS_SetReg(SISCR, rega, reg);
			reg = SiS_GetReg(SISCR, rega);
			reg = SiS_GetReg(SISCR, rega);
			reg += 0x10;
		}
	}

	SiS_SetRegAND(SISCR, 0x6e, 0xfc);

	ptr  = NULL;
	if(ivideo->haveXGIROM) {
		index = (ivideo->chip == XGI_20) ? 0x35a : 0x3e6;
		ptr  = (const u8 *)&bios[index];
	}
	for(i = 0; i < 4; i++) {
		SiS_SetRegANDOR(SISCR, 0x6e, 0xfc, i);
		reg = 0x00;
		for(j = 0; j < 2; j++) {
			regd = 0;
			if(ptr) {
				regd = le32_to_cpu(((u32 *)ptr)[regb * 8]);
				ptr += 4;
			}
			/* reg = 0x00; */
			for(k = 0; k < 16; k++) {
				reg &= 0xfc;
				if(regd & 0x01) reg |= 0x01;
				if(regd & 0x02) reg |= 0x02;
				regd >>= 2;
				SiS_SetReg(SISCR, 0x6f, reg);
				reg = SiS_GetReg(SISCR, 0x6f);
				reg = SiS_GetReg(SISCR, 0x6f);
				reg += 0x08;
			}
		}
	}

	ptr  = cs148;
	if(ivideo->haveXGIROM) {
		ptr  = (const u8 *)&bios[0x148];
	}
	for(i = 0, j = 0; i < 2; i++, j += 8) {
		SiS_SetReg(SISCR, 0x80 + i, ptr[j + regb]);
	}

	SiS_SetRegAND(SISCR, 0x89, 0x8f);

	ptr  = cs45a;
	if(ivideo->haveXGIROM) {
		index = (ivideo->chip == XGI_20) ? 0x45a : 0x4e6;
		ptr  = (const u8 *)&bios[index];
	}
	regd = le16_to_cpu(((const u16 *)ptr)[regb]);
	reg = 0x80;
	for(i = 0; i < 5; i++) {
		reg &= 0xfc;
		if(regd & 0x01) reg |= 0x01;
		if(regd & 0x02) reg |= 0x02;
		regd >>= 2;
		SiS_SetReg(SISCR, 0x89, reg);
		reg = SiS_GetReg(SISCR, 0x89);
		reg = SiS_GetReg(SISCR, 0x89);
		reg += 0x10;
	}

	v1 = 0xb5; v2 = 0x20; v3 = 0xf0; v4 = 0x13;
	if(ivideo->haveXGIROM) {
		v1 = bios[0x118 + regb];
		v2 = bios[0xf8 + regb];
		v3 = bios[0x120 + regb];
		v4 = bios[0x1ca];
	}
	SiS_SetReg(SISCR, 0x45, v1 & 0x0f);
	SiS_SetReg(SISCR, 0x99, (v1 >> 4) & 0x07);
	SiS_SetRegOR(SISCR, 0x40, v1 & 0x80);
	SiS_SetReg(SISCR, 0x41, v2);

	ptr  = cs170;
	if(ivideo->haveXGIROM) {
		ptr  = (const u8 *)&bios[0x170];
	}
	for(i = 0, j = 0; i < 7; i++, j += 8) {
		SiS_SetReg(SISCR, 0x90 + i, ptr[j + regb]);
	}

	SiS_SetReg(SISCR, 0x59, v3);

	ptr  = cs1a8;
	if(ivideo->haveXGIROM) {
		ptr  = (const u8 *)&bios[0x1a8];
	}
	for(i = 0, j = 0; i < 3; i++, j += 8) {
		SiS_SetReg(SISCR, 0xc3 + i, ptr[j + regb]);
	}

	ptr  = cs100;
	if(ivideo->haveXGIROM) {
		ptr  = (const u8 *)&bios[0x100];
	}
	for(i = 0, j = 0; i < 2; i++, j += 8) {
		SiS_SetReg(SISCR, 0x8a + i, ptr[j + regb]);
	}

	SiS_SetReg(SISCR, 0xcf, v4);

	SiS_SetReg(SISCR, 0x83, 0x09);
	SiS_SetReg(SISCR, 0x87, 0x00);

	if(ivideo->chip == XGI_40) {
		if( (ivideo->revision_id == 1) ||
		    (ivideo->revision_id == 2) ) {
			SiS_SetReg(SISCR, 0x8c, 0x87);
		}
	}

	if (regb == 1)
		SiS_SetReg(SISSR, 0x17, 0x80);		/* DDR2 */
	else
		SiS_SetReg(SISSR, 0x17, 0x00);		/* DDR1 */
	SiS_SetReg(SISSR, 0x1a, 0x87);

	if(ivideo->chip == XGI_20) {
		SiS_SetReg(SISSR, 0x15, 0x00);
		SiS_SetReg(SISSR, 0x1c, 0x00);
	}

	switch(ramtype) {
	case 0:
		sisfb_post_xgi_setclocks(ivideo, regb);
		if((ivideo->chip == XGI_20) ||
		   (ivideo->revision_id == 1)   ||
		   (ivideo->revision_id == 2)) {
			v1 = cs158[regb]; v2 = cs160[regb]; v3 = cs168[regb];
			if(ivideo->haveXGIROM) {
				v1 = bios[regb + 0x158];
				v2 = bios[regb + 0x160];
				v3 = bios[regb + 0x168];
			}
			SiS_SetReg(SISCR, 0x82, v1);
			SiS_SetReg(SISCR, 0x85, v2);
			SiS_SetReg(SISCR, 0x86, v3);
		} else {
			SiS_SetReg(SISCR, 0x82, 0x88);
			SiS_SetReg(SISCR, 0x86, 0x00);
			reg = SiS_GetReg(SISCR, 0x86);
			SiS_SetReg(SISCR, 0x86, 0x88);
			reg = SiS_GetReg(SISCR, 0x86);
			SiS_SetReg(SISCR, 0x86, bios[regb + 0x168]);
			SiS_SetReg(SISCR, 0x82, 0x77);
			SiS_SetReg(SISCR, 0x85, 0x00);
			reg = SiS_GetReg(SISCR, 0x85);
			SiS_SetReg(SISCR, 0x85, 0x88);
			reg = SiS_GetReg(SISCR, 0x85);
			SiS_SetReg(SISCR, 0x85, bios[regb + 0x160]);
			SiS_SetReg(SISCR, 0x82, bios[regb + 0x158]);
		}
		if(ivideo->chip == XGI_40) {
			SiS_SetReg(SISCR, 0x97, 0x00);
		}
		SiS_SetReg(SISCR, 0x98, 0x01);
		SiS_SetReg(SISCR, 0x9a, 0x02);

		SiS_SetReg(SISSR, 0x18, 0x01);
		if((ivideo->chip == XGI_20) ||
		   (ivideo->revision_id == 2)) {
			SiS_SetReg(SISSR, 0x19, 0x40);
		} else {
			SiS_SetReg(SISSR, 0x19, 0x20);
		}
		SiS_SetReg(SISSR, 0x16, 0x00);
		SiS_SetReg(SISSR, 0x16, 0x80);
		if((ivideo->chip == XGI_20) || (bios[0x1cb] != 0x0c)) {
			sisfb_post_xgi_delay(ivideo, 0x43);
			sisfb_post_xgi_delay(ivideo, 0x43);
			sisfb_post_xgi_delay(ivideo, 0x43);
			SiS_SetReg(SISSR, 0x18, 0x00);
			if((ivideo->chip == XGI_20) ||
			   (ivideo->revision_id == 2)) {
				SiS_SetReg(SISSR, 0x19, 0x40);
			} else {
				SiS_SetReg(SISSR, 0x19, 0x20);
			}
		} else if((ivideo->chip == XGI_40) && (bios[0x1cb] == 0x0c)) {
			/* SiS_SetReg(SISSR, 0x16, 0x0c); */ /* ? */
		}
		SiS_SetReg(SISSR, 0x16, 0x00);
		SiS_SetReg(SISSR, 0x16, 0x80);
		sisfb_post_xgi_delay(ivideo, 4);
		v1 = 0x31; v2 = 0x03; v3 = 0x83; v4 = 0x03; v5 = 0x83;
		if(ivideo->haveXGIROM) {
			v1 = bios[0xf0];
			index = (ivideo->chip == XGI_20) ? 0x4b2 : 0x53e;
			v2 = bios[index];
			v3 = bios[index + 1];
			v4 = bios[index + 2];
			v5 = bios[index + 3];
		}
		SiS_SetReg(SISSR, 0x18, v1);
		SiS_SetReg(SISSR, 0x19, ((ivideo->chip == XGI_20) ? 0x02 : 0x01));
		SiS_SetReg(SISSR, 0x16, v2);
		SiS_SetReg(SISSR, 0x16, v3);
		sisfb_post_xgi_delay(ivideo, 0x43);
		SiS_SetReg(SISSR, 0x1b, 0x03);
		sisfb_post_xgi_delay(ivideo, 0x22);
		SiS_SetReg(SISSR, 0x18, v1);
		SiS_SetReg(SISSR, 0x19, 0x00);
		SiS_SetReg(SISSR, 0x16, v4);
		SiS_SetReg(SISSR, 0x16, v5);
		SiS_SetReg(SISSR, 0x1b, 0x00);
		break;
	case 1:
		sisfb_post_xgi_ddr2(ivideo, regb);
		break;
	default:
		sisfb_post_xgi_setclocks(ivideo, regb);
		if((ivideo->chip == XGI_40) &&
		   ((ivideo->revision_id == 1) ||
		    (ivideo->revision_id == 2))) {
			SiS_SetReg(SISCR, 0x82, bios[regb + 0x158]);
			SiS_SetReg(SISCR, 0x85, bios[regb + 0x160]);
			SiS_SetReg(SISCR, 0x86, bios[regb + 0x168]);
		} else {
			SiS_SetReg(SISCR, 0x82, 0x88);
			SiS_SetReg(SISCR, 0x86, 0x00);
			reg = SiS_GetReg(SISCR, 0x86);
			SiS_SetReg(SISCR, 0x86, 0x88);
			SiS_SetReg(SISCR, 0x82, 0x77);
			SiS_SetReg(SISCR, 0x85, 0x00);
			reg = SiS_GetReg(SISCR, 0x85);
			SiS_SetReg(SISCR, 0x85, 0x88);
			reg = SiS_GetReg(SISCR, 0x85);
			v1 = cs160[regb]; v2 = cs158[regb];
			if(ivideo->haveXGIROM) {
				v1 = bios[regb + 0x160];
				v2 = bios[regb + 0x158];
			}
			SiS_SetReg(SISCR, 0x85, v1);
			SiS_SetReg(SISCR, 0x82, v2);
		}
		if(ivideo->chip == XGI_40) {
			SiS_SetReg(SISCR, 0x97, 0x11);
		}
		if((ivideo->chip == XGI_40) && (ivideo->revision_id == 2)) {
			SiS_SetReg(SISCR, 0x98, 0x01);
		} else {
			SiS_SetReg(SISCR, 0x98, 0x03);
		}
		SiS_SetReg(SISCR, 0x9a, 0x02);

		if(ivideo->chip == XGI_40) {
			SiS_SetReg(SISSR, 0x18, 0x01);
		} else {
			SiS_SetReg(SISSR, 0x18, 0x00);
		}
		SiS_SetReg(SISSR, 0x19, 0x40);
		SiS_SetReg(SISSR, 0x16, 0x00);
		SiS_SetReg(SISSR, 0x16, 0x80);
		if((ivideo->chip == XGI_40) && (bios[0x1cb] != 0x0c)) {
			sisfb_post_xgi_delay(ivideo, 0x43);
			sisfb_post_xgi_delay(ivideo, 0x43);
			sisfb_post_xgi_delay(ivideo, 0x43);
			SiS_SetReg(SISSR, 0x18, 0x00);
			SiS_SetReg(SISSR, 0x19, 0x40);
			SiS_SetReg(SISSR, 0x16, 0x00);
			SiS_SetReg(SISSR, 0x16, 0x80);
		}
		sisfb_post_xgi_delay(ivideo, 4);
		v1 = 0x31;
		if(ivideo->haveXGIROM) {
			v1 = bios[0xf0];
		}
		SiS_SetReg(SISSR, 0x18, v1);
		SiS_SetReg(SISSR, 0x19, 0x01);
		if(ivideo->chip == XGI_40) {
			SiS_SetReg(SISSR, 0x16, bios[0x53e]);
			SiS_SetReg(SISSR, 0x16, bios[0x53f]);
		} else {
			SiS_SetReg(SISSR, 0x16, 0x05);
			SiS_SetReg(SISSR, 0x16, 0x85);
		}
		sisfb_post_xgi_delay(ivideo, 0x43);
		if(ivideo->chip == XGI_40) {
			SiS_SetReg(SISSR, 0x1b, 0x01);
		} else {
			SiS_SetReg(SISSR, 0x1b, 0x03);
		}
		sisfb_post_xgi_delay(ivideo, 0x22);
		SiS_SetReg(SISSR, 0x18, v1);
		SiS_SetReg(SISSR, 0x19, 0x00);
		if(ivideo->chip == XGI_40) {
			SiS_SetReg(SISSR, 0x16, bios[0x540]);
			SiS_SetReg(SISSR, 0x16, bios[0x541]);
		} else {
			SiS_SetReg(SISSR, 0x16, 0x05);
			SiS_SetReg(SISSR, 0x16, 0x85);
		}
		SiS_SetReg(SISSR, 0x1b, 0x00);
	}

	regb = 0;	/* ! */
	v1 = 0x03;
	if(ivideo->haveXGIROM) {
		v1 = bios[0x110 + regb];
	}
	SiS_SetReg(SISSR, 0x1b, v1);

	/* RAM size */
	v1 = 0x00; v2 = 0x00;
	if(ivideo->haveXGIROM) {
		v1 = bios[0x62];
		v2 = bios[0x63];
	}
	regb = 0;	/* ! */
	regd = 1 << regb;
	if((v1 & 0x40) && (v2 & regd) && ivideo->haveXGIROM) {

		SiS_SetReg(SISSR, 0x13, bios[regb + 0xe0]);
		SiS_SetReg(SISSR, 0x14, bios[regb + 0xe0 + 8]);

	} else {
		int err;

		/* Set default mode, don't clear screen */
		ivideo->SiS_Pr.SiS_UseOEM = false;
		SiS_SetEnableDstn(&ivideo->SiS_Pr, false);
		SiS_SetEnableFstn(&ivideo->SiS_Pr, false);
		ivideo->curFSTN = ivideo->curDSTN = 0;
		ivideo->SiS_Pr.VideoMemorySize = 8 << 20;
		SiSSetMode(&ivideo->SiS_Pr, 0x2e | 0x80);

		SiS_SetReg(SISSR, 0x05, 0x86);

		/* Disable read-cache */
		SiS_SetRegAND(SISSR, 0x21, 0xdf);
		err = sisfb_post_xgi_ramsize(ivideo);
		/* Enable read-cache */
		SiS_SetRegOR(SISSR, 0x21, 0x20);

		if (err) {
			dev_err(&pdev->dev,
				"%s: RAM size detection failed: %d\n",
				__func__, err);
			return 0;
		}
	}

#if 0
	printk(KERN_DEBUG "-----------------\n");
	for(i = 0; i < 0xff; i++) {
		reg = SiS_GetReg(SISCR, i);
		printk(KERN_DEBUG "CR%02x(%x) = 0x%02x\n", i, SISCR, reg);
	}
	for(i = 0; i < 0x40; i++) {
		reg = SiS_GetReg(SISSR, i);
		printk(KERN_DEBUG "SR%02x(%x) = 0x%02x\n", i, SISSR, reg);
	}
	printk(KERN_DEBUG "-----------------\n");
#endif

	/* Sense CRT1 */
	if(ivideo->chip == XGI_20) {
		SiS_SetRegOR(SISCR, 0x32, 0x20);
	} else {
		reg = SiS_GetReg(SISPART4, 0x00);
		if((reg == 1) || (reg == 2)) {
			sisfb_sense_crt1(ivideo);
		} else {
			SiS_SetRegOR(SISCR, 0x32, 0x20);
		}
	}

	/* Set default mode, don't clear screen */
	ivideo->SiS_Pr.SiS_UseOEM = false;
	SiS_SetEnableDstn(&ivideo->SiS_Pr, false);
	SiS_SetEnableFstn(&ivideo->SiS_Pr, false);
	ivideo->curFSTN = ivideo->curDSTN = 0;
	SiSSetMode(&ivideo->SiS_Pr, 0x2e | 0x80);

	SiS_SetReg(SISSR, 0x05, 0x86);

	/* Display off */
	SiS_SetRegOR(SISSR, 0x01, 0x20);

	/* Save mode number in CR34 */
	SiS_SetReg(SISCR, 0x34, 0x2e);

	/* Let everyone know what the current mode is */
	ivideo->modeprechange = 0x2e;

	if(ivideo->chip == XGI_40) {
		reg = SiS_GetReg(SISCR, 0xca);
		v1 = SiS_GetReg(SISCR, 0xcc);
		if((reg & 0x10) && (!(v1 & 0x04))) {
			printk(KERN_ERR
				"sisfb: Please connect power to the card.\n");
			return 0;
		}
	}

	return 1;
}
#endif

static int sisfb_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct sisfb_chip_info	*chipinfo = &sisfb_chip_info[ent->driver_data];
	struct sis_video_info	*ivideo = NULL;
	struct fb_info		*sis_fb_info = NULL;
	u16 reg16;
	u8  reg;
	int i, ret;

	if(sisfb_off)
		return -ENXIO;

	ret = aperture_remove_conflicting_pci_devices(pdev, "sisfb");
	if (ret)
		return ret;

	sis_fb_info = framebuffer_alloc(sizeof(*ivideo), &pdev->dev);
	if(!sis_fb_info)
		return -ENOMEM;

	ivideo = (struct sis_video_info *)sis_fb_info->par;
	ivideo->memyselfandi = sis_fb_info;

	ivideo->sisfb_id = SISFB_ID;

	if(card_list == NULL) {
		ivideo->cardnumber = 0;
	} else {
		struct sis_video_info *countvideo = card_list;
		ivideo->cardnumber = 1;
		while((countvideo = countvideo->next) != NULL)
			ivideo->cardnumber++;
	}

	strscpy(ivideo->myid, chipinfo->chip_name, sizeof(ivideo->myid));

	ivideo->warncount = 0;
	ivideo->chip_id = pdev->device;
	ivideo->chip_vendor = pdev->vendor;
	ivideo->revision_id = pdev->revision;
	ivideo->SiS_Pr.ChipRevision = ivideo->revision_id;
	pci_read_config_word(pdev, PCI_COMMAND, &reg16);
	ivideo->sisvga_enabled = reg16 & 0x01;
	ivideo->pcibus = pdev->bus->number;
	ivideo->pcislot = PCI_SLOT(pdev->devfn);
	ivideo->pcifunc = PCI_FUNC(pdev->devfn);
	ivideo->subsysvendor = pdev->subsystem_vendor;
	ivideo->subsysdevice = pdev->subsystem_device;

#ifndef MODULE
	if(sisfb_mode_idx == -1) {
		sisfb_get_vga_mode_from_kernel();
	}
#endif

	ivideo->chip = chipinfo->chip;
	ivideo->chip_real_id = chipinfo->chip;
	ivideo->sisvga_engine = chipinfo->vgaengine;
	ivideo->hwcursor_size = chipinfo->hwcursor_size;
	ivideo->CRT2_write_enable = chipinfo->CRT2_write_enable;
	ivideo->mni = chipinfo->mni;

	ivideo->detectedpdc  = 0xff;
	ivideo->detectedpdca = 0xff;
	ivideo->detectedlcda = 0xff;

	ivideo->sisfb_thismonitor.datavalid = false;

	ivideo->current_base = 0;

	ivideo->engineok = 0;

	ivideo->sisfb_was_boot_device = 0;

	if(pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW) {
		if(ivideo->sisvga_enabled)
			ivideo->sisfb_was_boot_device = 1;
		else {
			printk(KERN_DEBUG "sisfb: PCI device is disabled, "
				"but marked as boot video device ???\n");
			printk(KERN_DEBUG "sisfb: I will not accept this "
				"as the primary VGA device\n");
		}
	}

	ivideo->sisfb_parm_mem = sisfb_parm_mem;
	ivideo->sisfb_accel = sisfb_accel;
	ivideo->sisfb_ypan = sisfb_ypan;
	ivideo->sisfb_max = sisfb_max;
	ivideo->sisfb_userom = sisfb_userom;
	ivideo->sisfb_useoem = sisfb_useoem;
	ivideo->sisfb_mode_idx = sisfb_mode_idx;
	ivideo->sisfb_parm_rate = sisfb_parm_rate;
	ivideo->sisfb_crt1off = sisfb_crt1off;
	ivideo->sisfb_forcecrt1 = sisfb_forcecrt1;
	ivideo->sisfb_crt2type = sisfb_crt2type;
	ivideo->sisfb_crt2flags = sisfb_crt2flags;
	/* pdc(a), scalelcd, special timing, lvdshl handled below */
	ivideo->sisfb_dstn = sisfb_dstn;
	ivideo->sisfb_fstn = sisfb_fstn;
	ivideo->sisfb_tvplug = sisfb_tvplug;
	ivideo->sisfb_tvstd = sisfb_tvstd;
	ivideo->tvxpos = sisfb_tvxposoffset;
	ivideo->tvypos = sisfb_tvyposoffset;
	ivideo->sisfb_nocrt2rate = sisfb_nocrt2rate;
	ivideo->refresh_rate = 0;
	if(ivideo->sisfb_parm_rate != -1) {
		ivideo->refresh_rate = ivideo->sisfb_parm_rate;
	}

	ivideo->SiS_Pr.UsePanelScaler = sisfb_scalelcd;
	ivideo->SiS_Pr.CenterScreen = -1;
	ivideo->SiS_Pr.SiS_CustomT = sisfb_specialtiming;
	ivideo->SiS_Pr.LVDSHL = sisfb_lvdshl;

	ivideo->SiS_Pr.SiS_Backup70xx = 0xff;
	ivideo->SiS_Pr.SiS_CHOverScan = -1;
	ivideo->SiS_Pr.SiS_ChSW = false;
	ivideo->SiS_Pr.SiS_UseLCDA = false;
	ivideo->SiS_Pr.HaveEMI = false;
	ivideo->SiS_Pr.HaveEMILCD = false;
	ivideo->SiS_Pr.OverruleEMI = false;
	ivideo->SiS_Pr.SiS_SensibleSR11 = false;
	ivideo->SiS_Pr.SiS_MyCR63 = 0x63;
	ivideo->SiS_Pr.PDC  = -1;
	ivideo->SiS_Pr.PDCA = -1;
	ivideo->SiS_Pr.DDCPortMixup = false;
#ifdef CONFIG_FB_SIS_315
	if(ivideo->chip >= SIS_330) {
		ivideo->SiS_Pr.SiS_MyCR63 = 0x53;
		if(ivideo->chip >= SIS_661) {
			ivideo->SiS_Pr.SiS_SensibleSR11 = true;
		}
	}
#endif

	memcpy(&ivideo->default_var, &my_default_var, sizeof(my_default_var));

	pci_set_drvdata(pdev, ivideo);

	/* Patch special cases */
	if((ivideo->nbridge = sisfb_get_northbridge(ivideo->chip))) {
		switch(ivideo->nbridge->device) {
#ifdef CONFIG_FB_SIS_300
		case PCI_DEVICE_ID_SI_730:
			ivideo->chip = SIS_730;
			strcpy(ivideo->myid, "SiS 730");
			break;
#endif
#ifdef CONFIG_FB_SIS_315
		case PCI_DEVICE_ID_SI_651:
			/* ivideo->chip is ok */
			strcpy(ivideo->myid, "SiS 651");
			break;
		case PCI_DEVICE_ID_SI_740:
			ivideo->chip = SIS_740;
			strcpy(ivideo->myid, "SiS 740");
			break;
		case PCI_DEVICE_ID_SI_661:
			ivideo->chip = SIS_661;
			strcpy(ivideo->myid, "SiS 661");
			break;
		case PCI_DEVICE_ID_SI_741:
			ivideo->chip = SIS_741;
			strcpy(ivideo->myid, "SiS 741");
			break;
		case PCI_DEVICE_ID_SI_760:
			ivideo->chip = SIS_760;
			strcpy(ivideo->myid, "SiS 760");
			break;
		case PCI_DEVICE_ID_SI_761:
			ivideo->chip = SIS_761;
			strcpy(ivideo->myid, "SiS 761");
			break;
#endif
		default:
			break;
		}
	}

	ivideo->SiS_Pr.ChipType = ivideo->chip;

	ivideo->SiS_Pr.ivideo = (void *)ivideo;

#ifdef CONFIG_FB_SIS_315
	if((ivideo->SiS_Pr.ChipType == SIS_315PRO) ||
	   (ivideo->SiS_Pr.ChipType == SIS_315)) {
		ivideo->SiS_Pr.ChipType = SIS_315H;
	}
#endif

	if(!ivideo->sisvga_enabled) {
		if(pci_enable_device(pdev)) {
			pci_dev_put(ivideo->nbridge);
			framebuffer_release(sis_fb_info);
			return -EIO;
		}
	}

	ivideo->video_base = pci_resource_start(pdev, 0);
	ivideo->video_size = pci_resource_len(pdev, 0);
	ivideo->mmio_base  = pci_resource_start(pdev, 1);
	ivideo->mmio_size  = pci_resource_len(pdev, 1);
	ivideo->SiS_Pr.RelIO = pci_resource_start(pdev, 2) + 0x30;
	ivideo->SiS_Pr.IOAddress = ivideo->vga_base = ivideo->SiS_Pr.RelIO;

	SiSRegInit(&ivideo->SiS_Pr, ivideo->SiS_Pr.IOAddress);

#ifdef CONFIG_FB_SIS_300
	/* Find PCI systems for Chrontel/GPIO communication setup */
	if(ivideo->chip == SIS_630) {
		i = 0;
        	do {
			if(mychswtable[i].subsysVendor == ivideo->subsysvendor &&
			   mychswtable[i].subsysCard   == ivideo->subsysdevice) {
				ivideo->SiS_Pr.SiS_ChSW = true;
				printk(KERN_DEBUG "sisfb: Identified [%s %s] "
					"requiring Chrontel/GPIO setup\n",
					mychswtable[i].vendorName,
					mychswtable[i].cardName);
				ivideo->lpcdev = pci_get_device(PCI_VENDOR_ID_SI, 0x0008, NULL);
				break;
			}
			i++;
		} while(mychswtable[i].subsysVendor != 0);
	}
#endif

#ifdef CONFIG_FB_SIS_315
	if((ivideo->chip == SIS_760) && (ivideo->nbridge)) {
		ivideo->lpcdev = pci_get_slot(ivideo->nbridge->bus, (2 << 3));
	}
#endif

	SiS_SetReg(SISSR, 0x05, 0x86);

	if( (!ivideo->sisvga_enabled)
#if !defined(__i386__) && !defined(__x86_64__)
			      || (sisfb_resetcard)
#endif
						   ) {
		for(i = 0x30; i <= 0x3f; i++) {
			SiS_SetReg(SISCR, i, 0x00);
		}
	}

	/* Find out about current video mode */
	ivideo->modeprechange = 0x03;
	reg = SiS_GetReg(SISCR, 0x34);
	if(reg & 0x7f) {
		ivideo->modeprechange = reg & 0x7f;
	} else if(ivideo->sisvga_enabled) {
#if defined(__i386__) || defined(__x86_64__)
		unsigned char __iomem *tt = ioremap(0x400, 0x100);
		if(tt) {
			ivideo->modeprechange = readb(tt + 0x49);
			iounmap(tt);
		}
#endif
	}

	/* Search and copy ROM image */
	ivideo->bios_abase = NULL;
	ivideo->SiS_Pr.VirtualRomBase = NULL;
	ivideo->SiS_Pr.UseROM = false;
	ivideo->haveXGIROM = ivideo->SiS_Pr.SiS_XGIROM = false;
	if(ivideo->sisfb_userom) {
		ivideo->SiS_Pr.VirtualRomBase = sisfb_find_rom(pdev);
		ivideo->bios_abase = ivideo->SiS_Pr.VirtualRomBase;
		ivideo->SiS_Pr.UseROM = (bool)(ivideo->SiS_Pr.VirtualRomBase);
		printk(KERN_INFO "sisfb: Video ROM %sfound\n",
			ivideo->SiS_Pr.UseROM ? "" : "not ");
		if((ivideo->SiS_Pr.UseROM) && (ivideo->chip >= XGI_20)) {
		   ivideo->SiS_Pr.UseROM = false;
		   ivideo->haveXGIROM = ivideo->SiS_Pr.SiS_XGIROM = true;
		   if( (ivideo->revision_id == 2) &&
		       (!(ivideo->bios_abase[0x1d1] & 0x01)) ) {
			ivideo->SiS_Pr.DDCPortMixup = true;
		   }
		}
	} else {
		printk(KERN_INFO "sisfb: Video ROM usage disabled\n");
	}

	/* Find systems for special custom timing */
	if(ivideo->SiS_Pr.SiS_CustomT == CUT_NONE) {
		sisfb_detect_custom_timing(ivideo);
	}

#ifdef CONFIG_FB_SIS_315
	if (ivideo->chip == XGI_20) {
		/* Check if our Z7 chip is actually Z9 */
		SiS_SetRegOR(SISCR, 0x4a, 0x40);	/* GPIOG EN */
		reg = SiS_GetReg(SISCR, 0x48);
		if (reg & 0x02) {			/* GPIOG */
			ivideo->chip_real_id = XGI_21;
			dev_info(&pdev->dev, "Z9 detected\n");
		}
	}
#endif

	/* POST card in case this has not been done by the BIOS */
	if( (!ivideo->sisvga_enabled)
#if !defined(__i386__) && !defined(__x86_64__)
			     || (sisfb_resetcard)
#endif
						 ) {
#ifdef CONFIG_FB_SIS_300
		if(ivideo->sisvga_engine == SIS_300_VGA) {
			if(ivideo->chip == SIS_300) {
				sisfb_post_sis300(pdev);
				ivideo->sisfb_can_post = 1;
			}
		}
#endif

#ifdef CONFIG_FB_SIS_315
		if (ivideo->sisvga_engine == SIS_315_VGA) {
			int result = 1;

			if (ivideo->chip == XGI_20) {
				result = sisfb_post_xgi(pdev);
				ivideo->sisfb_can_post = 1;
			} else if ((ivideo->chip == XGI_40) && ivideo->haveXGIROM) {
				result = sisfb_post_xgi(pdev);
				ivideo->sisfb_can_post = 1;
			} else {
				printk(KERN_INFO "sisfb: Card is not "
					"POSTed and sisfb can't do this either.\n");
			}
			if (!result) {
				printk(KERN_ERR "sisfb: Failed to POST card\n");
				ret = -ENODEV;
				goto error_3;
			}
		}
#endif
	}

	ivideo->sisfb_card_posted = 1;

	/* Find out about RAM size */
	if(sisfb_get_dram_size(ivideo)) {
		printk(KERN_INFO "sisfb: Fatal error: Unable to determine VRAM size.\n");
		ret = -ENODEV;
		goto error_3;
	}


	/* Enable PCI addressing and MMIO */
	if((ivideo->sisfb_mode_idx < 0) ||
	   ((sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni]) != 0xFF)) {
		/* Enable PCI_LINEAR_ADDRESSING and MMIO_ENABLE  */
		SiS_SetRegOR(SISSR, IND_SIS_PCI_ADDRESS_SET, (SIS_PCI_ADDR_ENABLE | SIS_MEM_MAP_IO_ENABLE));
		/* Enable 2D accelerator engine */
		SiS_SetRegOR(SISSR, IND_SIS_MODULE_ENABLE, SIS_ENABLE_2D);
	}

	if(sisfb_pdc != 0xff) {
		if(ivideo->sisvga_engine == SIS_300_VGA)
			sisfb_pdc &= 0x3c;
		else
			sisfb_pdc &= 0x1f;
		ivideo->SiS_Pr.PDC = sisfb_pdc;
	}
#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
		if(sisfb_pdca != 0xff)
			ivideo->SiS_Pr.PDCA = sisfb_pdca & 0x1f;
	}
#endif

	if(!request_mem_region(ivideo->video_base, ivideo->video_size, "sisfb FB")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve %dMB framebuffer memory\n",
				(int)(ivideo->video_size >> 20));
		printk(KERN_ERR "sisfb: Is there another framebuffer driver active?\n");
		ret = -ENODEV;
		goto error_3;
	}

	if(!request_mem_region(ivideo->mmio_base, ivideo->mmio_size, "sisfb MMIO")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve MMIO region\n");
		ret = -ENODEV;
		goto error_2;
	}

	ivideo->video_vbase = ioremap_wc(ivideo->video_base, ivideo->video_size);
	ivideo->SiS_Pr.VideoMemoryAddress = ivideo->video_vbase;
	if(!ivideo->video_vbase) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to map framebuffer memory\n");
		ret = -ENODEV;
		goto error_1;
	}

	ivideo->mmio_vbase = ioremap(ivideo->mmio_base, ivideo->mmio_size);
	if(!ivideo->mmio_vbase) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to map MMIO region\n");
		ret = -ENODEV;
error_0:	iounmap(ivideo->video_vbase);
error_1:	release_mem_region(ivideo->video_base, ivideo->video_size);
error_2:	release_mem_region(ivideo->mmio_base, ivideo->mmio_size);
error_3:	vfree(ivideo->bios_abase);
		pci_dev_put(ivideo->lpcdev);
		pci_dev_put(ivideo->nbridge);
		if(!ivideo->sisvga_enabled)
			pci_disable_device(pdev);
		framebuffer_release(sis_fb_info);
		return ret;
	}

	printk(KERN_INFO "sisfb: Video RAM at 0x%lx, mapped to 0x%lx, size %ldk\n",
		ivideo->video_base, (unsigned long)ivideo->video_vbase, ivideo->video_size / 1024);

	if(ivideo->video_offset) {
		printk(KERN_INFO "sisfb: Viewport offset %ldk\n",
			ivideo->video_offset / 1024);
	}

	printk(KERN_INFO "sisfb: MMIO at 0x%lx, mapped to 0x%lx, size %ldk\n",
		ivideo->mmio_base, (unsigned long)ivideo->mmio_vbase, ivideo->mmio_size / 1024);


	/* Determine the size of the command queue */
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		ivideo->cmdQueueSize = TURBO_QUEUE_AREA_SIZE;
	} else {
		if(ivideo->chip == XGI_20) {
			ivideo->cmdQueueSize = COMMAND_QUEUE_AREA_SIZE_Z7;
		} else {
			ivideo->cmdQueueSize = COMMAND_QUEUE_AREA_SIZE;
		}
	}

	/* Engines are no longer initialized here; this is
	 * now done after the first mode-switch (if the
	 * submitted var has its acceleration flags set).
	 */

	/* Calculate the base of the (unused) hw cursor */
	ivideo->hwcursor_vbase = ivideo->video_vbase
				 + ivideo->video_size
				 - ivideo->cmdQueueSize
				 - ivideo->hwcursor_size;
	ivideo->caps |= HW_CURSOR_CAP;

	/* Initialize offscreen memory manager */
	if((ivideo->havenoheap = sisfb_heap_init(ivideo))) {
		printk(KERN_WARNING "sisfb: Failed to initialize offscreen memory heap\n");
	}

	/* Used for clearing the screen only, therefore respect our mem limit */
	ivideo->SiS_Pr.VideoMemoryAddress += ivideo->video_offset;
	ivideo->SiS_Pr.VideoMemorySize = ivideo->sisfb_mem;

	ivideo->vbflags = 0;
	ivideo->lcddefmodeidx = DEFAULT_LCDMODE;
	ivideo->tvdefmodeidx  = DEFAULT_TVMODE;
	ivideo->defmodeidx    = DEFAULT_MODE;

	ivideo->newrom = 0;
	if(ivideo->chip < XGI_20) {
		if(ivideo->bios_abase) {
			ivideo->newrom = SiSDetermineROMLayout661(&ivideo->SiS_Pr);
		}
	}

	if((ivideo->sisfb_mode_idx < 0) ||
	   ((sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni]) != 0xFF)) {

		sisfb_sense_crt1(ivideo);

		sisfb_get_VB_type(ivideo);

		if(ivideo->vbflags2 & VB2_VIDEOBRIDGE) {
			sisfb_detect_VB_connect(ivideo);
		}

		ivideo->currentvbflags = ivideo->vbflags & (VB_VIDEOBRIDGE | TV_STANDARD);

		/* Decide on which CRT2 device to use */
		if(ivideo->vbflags2 & VB2_VIDEOBRIDGE) {
			if(ivideo->sisfb_crt2type != -1) {
				if((ivideo->sisfb_crt2type == CRT2_LCD) &&
				   (ivideo->vbflags & CRT2_LCD)) {
					ivideo->currentvbflags |= CRT2_LCD;
				} else if(ivideo->sisfb_crt2type != CRT2_LCD) {
					ivideo->currentvbflags |= ivideo->sisfb_crt2type;
				}
			} else {
				/* Chrontel 700x TV detection often unreliable, therefore
				 * use a different default order on such machines
				 */
				if((ivideo->sisvga_engine == SIS_300_VGA) &&
				   (ivideo->vbflags2 & VB2_CHRONTEL)) {
					if(ivideo->vbflags & CRT2_LCD)
						ivideo->currentvbflags |= CRT2_LCD;
					else if(ivideo->vbflags & CRT2_TV)
						ivideo->currentvbflags |= CRT2_TV;
					else if(ivideo->vbflags & CRT2_VGA)
						ivideo->currentvbflags |= CRT2_VGA;
				} else {
					if(ivideo->vbflags & CRT2_TV)
						ivideo->currentvbflags |= CRT2_TV;
					else if(ivideo->vbflags & CRT2_LCD)
						ivideo->currentvbflags |= CRT2_LCD;
					else if(ivideo->vbflags & CRT2_VGA)
						ivideo->currentvbflags |= CRT2_VGA;
				}
			}
		}

		if(ivideo->vbflags & CRT2_LCD) {
			sisfb_detect_lcd_type(ivideo);
		}

		sisfb_save_pdc_emi(ivideo);

		if(!ivideo->sisfb_crt1off) {
			sisfb_handle_ddc(ivideo, &ivideo->sisfb_thismonitor, 0);
		} else {
			if((ivideo->vbflags2 & VB2_SISTMDSBRIDGE) &&
			   (ivideo->vbflags & (CRT2_VGA | CRT2_LCD))) {
				sisfb_handle_ddc(ivideo, &ivideo->sisfb_thismonitor, 1);
			}
		}

		if(ivideo->sisfb_mode_idx >= 0) {
			int bu = ivideo->sisfb_mode_idx;
			ivideo->sisfb_mode_idx = sisfb_validate_mode(ivideo,
					ivideo->sisfb_mode_idx, ivideo->currentvbflags);
			if(bu != ivideo->sisfb_mode_idx) {
				printk(KERN_ERR "Mode %dx%dx%d failed validation\n",
					sisbios_mode[bu].xres,
					sisbios_mode[bu].yres,
					sisbios_mode[bu].bpp);
			}
		}

		if(ivideo->sisfb_mode_idx < 0) {
			switch(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
			   case CRT2_LCD:
				ivideo->sisfb_mode_idx = ivideo->lcddefmodeidx;
				break;
			   case CRT2_TV:
				ivideo->sisfb_mode_idx = ivideo->tvdefmodeidx;
				break;
			   default:
				ivideo->sisfb_mode_idx = ivideo->defmodeidx;
				break;
			}
		}

		ivideo->mode_no = sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni];

		if(ivideo->refresh_rate != 0) {
			sisfb_search_refresh_rate(ivideo, ivideo->refresh_rate,
						ivideo->sisfb_mode_idx);
		}

		if(ivideo->rate_idx == 0) {
			ivideo->rate_idx = sisbios_mode[ivideo->sisfb_mode_idx].rate_idx;
			ivideo->refresh_rate = 60;
		}

		if(ivideo->sisfb_thismonitor.datavalid) {
			if(!sisfb_verify_rate(ivideo, &ivideo->sisfb_thismonitor,
						ivideo->sisfb_mode_idx,
						ivideo->rate_idx,
						ivideo->refresh_rate)) {
				printk(KERN_INFO "sisfb: WARNING: Refresh rate "
							"exceeds monitor specs!\n");
			}
		}

		ivideo->video_bpp = sisbios_mode[ivideo->sisfb_mode_idx].bpp;
		ivideo->video_width = sisbios_mode[ivideo->sisfb_mode_idx].xres;
		ivideo->video_height = sisbios_mode[ivideo->sisfb_mode_idx].yres;

		sisfb_set_vparms(ivideo);

		printk(KERN_INFO "sisfb: Default mode is %dx%dx%d (%dHz)\n",
			ivideo->video_width, ivideo->video_height, ivideo->video_bpp,
			ivideo->refresh_rate);

		/* Set up the default var according to chosen default display mode */
		ivideo->default_var.xres = ivideo->default_var.xres_virtual = ivideo->video_width;
		ivideo->default_var.yres = ivideo->default_var.yres_virtual = ivideo->video_height;
		ivideo->default_var.bits_per_pixel = ivideo->video_bpp;

		sisfb_bpp_to_var(ivideo, &ivideo->default_var);

		ivideo->default_var.pixclock = (u32) (1000000000 /
			sisfb_mode_rate_to_dclock(&ivideo->SiS_Pr, ivideo->mode_no, ivideo->rate_idx));

		if(sisfb_mode_rate_to_ddata(&ivideo->SiS_Pr, ivideo->mode_no,
						ivideo->rate_idx, &ivideo->default_var)) {
			if((ivideo->default_var.vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
				ivideo->default_var.pixclock <<= 1;
			}
		}

		if(ivideo->sisfb_ypan) {
			/* Maximize regardless of sisfb_max at startup */
			ivideo->default_var.yres_virtual =
				sisfb_calc_maxyres(ivideo, &ivideo->default_var);
			if(ivideo->default_var.yres_virtual < ivideo->default_var.yres) {
				ivideo->default_var.yres_virtual = ivideo->default_var.yres;
			}
		}

		sisfb_calc_pitch(ivideo, &ivideo->default_var);

		ivideo->accel = 0;
		if(ivideo->sisfb_accel) {
			ivideo->accel = -1;
#ifdef STUPID_ACCELF_TEXT_SHIT
			ivideo->default_var.accel_flags |= FB_ACCELF_TEXT;
#endif
		}
		sisfb_initaccel(ivideo);

#if defined(FBINFO_HWACCEL_DISABLED) && defined(FBINFO_HWACCEL_XPAN)
		sis_fb_info->flags = FBINFO_DEFAULT 		|
				     FBINFO_HWACCEL_YPAN 	|
				     FBINFO_HWACCEL_XPAN 	|
				     FBINFO_HWACCEL_COPYAREA 	|
				     FBINFO_HWACCEL_FILLRECT 	|
				     ((ivideo->accel) ? 0 : FBINFO_HWACCEL_DISABLED);
#else
		sis_fb_info->flags = FBINFO_FLAG_DEFAULT;
#endif
		sis_fb_info->var = ivideo->default_var;
		sis_fb_info->fix = ivideo->sisfb_fix;
		sis_fb_info->screen_base = ivideo->video_vbase + ivideo->video_offset;
		sis_fb_info->fbops = &sisfb_ops;
		sis_fb_info->pseudo_palette = ivideo->pseudo_palette;

		fb_alloc_cmap(&sis_fb_info->cmap, 256 , 0);

		printk(KERN_DEBUG "sisfb: Initial vbflags 0x%x\n", (int)ivideo->vbflags);

		ivideo->wc_cookie = arch_phys_wc_add(ivideo->video_base,
						     ivideo->video_size);
		if(register_framebuffer(sis_fb_info) < 0) {
			printk(KERN_ERR "sisfb: Fatal error: Failed to register framebuffer\n");
			ret = -EINVAL;
			iounmap(ivideo->mmio_vbase);
			goto error_0;
		}

		ivideo->registered = 1;

		/* Enlist us */
		ivideo->next = card_list;
		card_list = ivideo;

		printk(KERN_INFO "sisfb: 2D acceleration is %s, y-panning %s\n",
			ivideo->sisfb_accel ? "enabled" : "disabled",
			ivideo->sisfb_ypan  ?
				(ivideo->sisfb_max ? "enabled (auto-max)" :
						"enabled (no auto-max)") :
									"disabled");


		fb_info(sis_fb_info, "%s frame buffer device version %d.%d.%d\n",
			ivideo->myid, VER_MAJOR, VER_MINOR, VER_LEVEL);

		printk(KERN_INFO "sisfb: Copyright (C) 2001-2005 Thomas Winischhofer\n");

	}	/* if mode = "none" */

	return 0;
}

/*****************************************************/
/*                PCI DEVICE HANDLING                */
/*****************************************************/

static void sisfb_remove(struct pci_dev *pdev)
{
	struct sis_video_info	*ivideo = pci_get_drvdata(pdev);
	struct fb_info		*sis_fb_info = ivideo->memyselfandi;
	int			registered = ivideo->registered;
	int			modechanged = ivideo->modechanged;

	/* Unmap */
	iounmap(ivideo->mmio_vbase);
	iounmap(ivideo->video_vbase);

	/* Release mem regions */
	release_mem_region(ivideo->video_base, ivideo->video_size);
	release_mem_region(ivideo->mmio_base, ivideo->mmio_size);

	vfree(ivideo->bios_abase);

	pci_dev_put(ivideo->lpcdev);

	pci_dev_put(ivideo->nbridge);

	arch_phys_wc_del(ivideo->wc_cookie);

	/* If device was disabled when starting, disable
	 * it when quitting.
	 */
	if(!ivideo->sisvga_enabled)
		pci_disable_device(pdev);

	/* Unregister the framebuffer */
	if(ivideo->registered) {
		unregister_framebuffer(sis_fb_info);
		framebuffer_release(sis_fb_info);
	}

	/* OK, our ivideo is gone for good from here. */

	/* TODO: Restore the initial mode
	 * This sounds easy but is as good as impossible
	 * on many machines with SiS chip and video bridge
	 * since text modes are always set up differently
	 * from machine to machine. Depends on the type
	 * of integration between chipset and bridge.
	 */
	if(registered && modechanged)
		printk(KERN_INFO
			"sisfb: Restoring of text mode not supported yet\n");
};

static struct pci_driver sisfb_driver = {
	.name		= "sisfb",
	.id_table 	= sisfb_pci_table,
	.probe		= sisfb_probe,
	.remove 	= sisfb_remove,
};

static int __init sisfb_init(void)
{
#ifndef MODULE
	char *options = NULL;

	if(fb_get_options("sisfb", &options))
		return -ENODEV;

	sisfb_setup(options);
#endif
	return pci_register_driver(&sisfb_driver);
}

#ifndef MODULE
module_init(sisfb_init);
#endif

/*****************************************************/
/*                      MODULE                       */
/*****************************************************/

#ifdef MODULE

static char		*mode = NULL;
static int		vesa = -1;
static unsigned int	rate = 0;
static unsigned int	crt1off = 1;
static unsigned int	mem = 0;
static char		*forcecrt2type = NULL;
static int		forcecrt1 = -1;
static int		pdc = -1;
static int		pdc1 = -1;
static int		noaccel = -1;
static int		noypan  = -1;
static int		nomax = -1;
static int		userom = -1;
static int		useoem = -1;
static char		*tvstandard = NULL;
static int		nocrt2rate = 0;
static int		scalelcd = -1;
static char		*specialtiming = NULL;
static int		lvdshl = -1;
static int		tvxposoffset = 0, tvyposoffset = 0;
#if !defined(__i386__) && !defined(__x86_64__)
static int		resetcard = 0;
static int		videoram = 0;
#endif

static int __init sisfb_init_module(void)
{
	sisfb_setdefaultparms();

	if(rate)
		sisfb_parm_rate = rate;

	if((scalelcd == 0) || (scalelcd == 1))
		sisfb_scalelcd = scalelcd ^ 1;

	/* Need to check crt2 type first for fstn/dstn */

	if(forcecrt2type)
		sisfb_search_crt2type(forcecrt2type);

	if(tvstandard)
		sisfb_search_tvstd(tvstandard);

	if(mode)
		sisfb_search_mode(mode, false);
	else if(vesa != -1)
		sisfb_search_vesamode(vesa, false);

	sisfb_crt1off = (crt1off == 0) ? 1 : 0;

	sisfb_forcecrt1 = forcecrt1;
	if(forcecrt1 == 1)
		sisfb_crt1off = 0;
	else if(forcecrt1 == 0)
		sisfb_crt1off = 1;

	if(noaccel == 1)
		sisfb_accel = 0;
	else if(noaccel == 0)
		sisfb_accel = 1;

	if(noypan == 1)
		sisfb_ypan = 0;
	else if(noypan == 0)
		sisfb_ypan = 1;

	if(nomax == 1)
		sisfb_max = 0;
	else if(nomax == 0)
		sisfb_max = 1;

	if(mem)
		sisfb_parm_mem = mem;

	if(userom != -1)
		sisfb_userom = userom;

	if(useoem != -1)
		sisfb_useoem = useoem;

        if(pdc != -1)
		sisfb_pdc  = (pdc  & 0x7f);

	if(pdc1 != -1)
		sisfb_pdca = (pdc1 & 0x1f);

	sisfb_nocrt2rate = nocrt2rate;

	if(specialtiming)
		sisfb_search_specialtiming(specialtiming);

	if((lvdshl >= 0) && (lvdshl <= 3))
		sisfb_lvdshl = lvdshl;

	sisfb_tvxposoffset = tvxposoffset;
	sisfb_tvyposoffset = tvyposoffset;

#if !defined(__i386__) && !defined(__x86_64__)
	sisfb_resetcard = (resetcard) ? 1 : 0;
	if(videoram)
		sisfb_videoram = videoram;
#endif

	return sisfb_init();
}

static void __exit sisfb_remove_module(void)
{
	pci_unregister_driver(&sisfb_driver);
	printk(KERN_DEBUG "sisfb: Module unloaded\n");
}

module_init(sisfb_init_module);
module_exit(sisfb_remove_module);

MODULE_DESCRIPTION("SiS 300/540/630/730/315/55x/65x/661/74x/330/76x/34x, XGI V3XT/V5/V8/Z7 framebuffer device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Winischhofer <thomas@winischhofer.net>, Others");

module_param(mem, int, 0);
module_param(noaccel, int, 0);
module_param(noypan, int, 0);
module_param(nomax, int, 0);
module_param(userom, int, 0);
module_param(useoem, int, 0);
module_param(mode, charp, 0);
module_param(vesa, int, 0);
module_param(rate, int, 0);
module_param(forcecrt1, int, 0);
module_param(forcecrt2type, charp, 0);
module_param(scalelcd, int, 0);
module_param(pdc, int, 0);
module_param(pdc1, int, 0);
module_param(specialtiming, charp, 0);
module_param(lvdshl, int, 0);
module_param(tvstandard, charp, 0);
module_param(tvxposoffset, int, 0);
module_param(tvyposoffset, int, 0);
module_param(nocrt2rate, int, 0);
#if !defined(__i386__) && !defined(__x86_64__)
module_param(resetcard, int, 0);
module_param(videoram, int, 0);
#endif

MODULE_PARM_DESC(mem,
	"\nDetermines the beginning of the video memory heap in KB. This heap is used\n"
	  "for video RAM management for eg. DRM/DRI. On 300 series, the default depends\n"
	  "on the amount of video RAM available. If 8MB of video RAM or less is available,\n"
	  "the heap starts at 4096KB, if between 8 and 16MB are available at 8192KB,\n"
	  "otherwise at 12288KB. On 315/330/340 series, the heap size is 32KB by default.\n"
	  "The value is to be specified without 'KB'.\n");

MODULE_PARM_DESC(noaccel,
	"\nIf set to anything other than 0, 2D acceleration will be disabled.\n"
	  "(default: 0)\n");

MODULE_PARM_DESC(noypan,
	"\nIf set to anything other than 0, y-panning will be disabled and scrolling\n"
	  "will be performed by redrawing the screen. (default: 0)\n");

MODULE_PARM_DESC(nomax,
	"\nIf y-panning is enabled, sisfb will by default use the entire available video\n"
	  "memory for the virtual screen in order to optimize scrolling performance. If\n"
	  "this is set to anything other than 0, sisfb will not do this and thereby \n"
	  "enable the user to positively specify a virtual Y size of the screen using\n"
	  "fbset. (default: 0)\n");

MODULE_PARM_DESC(mode,
	"\nSelects the desired default display mode in the format XxYxDepth,\n"
	 "eg. 1024x768x16. Other formats supported include XxY-Depth and\n"
	 "XxY-Depth@Rate. If the parameter is only one (decimal or hexadecimal)\n"
	 "number, it will be interpreted as a VESA mode number. (default: 800x600x8)\n");

MODULE_PARM_DESC(vesa,
	"\nSelects the desired default display mode by VESA defined mode number, eg.\n"
	 "0x117 (default: 0x0103)\n");

MODULE_PARM_DESC(rate,
	"\nSelects the desired vertical refresh rate for CRT1 (external VGA) in Hz.\n"
	  "If the mode is specified in the format XxY-Depth@Rate, this parameter\n"
	  "will be ignored (default: 60)\n");

MODULE_PARM_DESC(forcecrt1,
	"\nNormally, the driver autodetects whether or not CRT1 (external VGA) is \n"
	  "connected. With this option, the detection can be overridden (1=CRT1 ON,\n"
	  "0=CRT1 OFF) (default: [autodetected])\n");

MODULE_PARM_DESC(forcecrt2type,
	"\nIf this option is omitted, the driver autodetects CRT2 output devices, such as\n"
	  "LCD, TV or secondary VGA. With this option, this autodetection can be\n"
	  "overridden. Possible parameters are LCD, TV, VGA or NONE. NONE disables CRT2.\n"
	  "On systems with a SiS video bridge, parameters SVIDEO, COMPOSITE or SCART can\n"
	  "be used instead of TV to override the TV detection. Furthermore, on systems\n"
	  "with a SiS video bridge, SVIDEO+COMPOSITE, HIVISION, YPBPR480I, YPBPR480P,\n"
	  "YPBPR720P and YPBPR1080I are understood. However, whether or not these work\n"
	  "depends on the very hardware in use. (default: [autodetected])\n");

MODULE_PARM_DESC(scalelcd,
	"\nSetting this to 1 will force the driver to scale the LCD image to the panel's\n"
	  "native resolution. Setting it to 0 will disable scaling; LVDS panels will\n"
	  "show black bars around the image, TMDS panels will probably do the scaling\n"
	  "themselves. Default: 1 on LVDS panels, 0 on TMDS panels\n");

MODULE_PARM_DESC(pdc,
	"\nThis is for manually selecting the LCD panel delay compensation. The driver\n"
	  "should detect this correctly in most cases; however, sometimes this is not\n"
	  "possible. If you see 'small waves' on the LCD, try setting this to 4, 32 or 24\n"
	  "on a 300 series chipset; 6 on other chipsets. If the problem persists, try\n"
	  "other values (on 300 series: between 4 and 60 in steps of 4; otherwise: any\n"
	  "value from 0 to 31). (default: autodetected, if LCD is active during start)\n");

#ifdef CONFIG_FB_SIS_315
MODULE_PARM_DESC(pdc1,
	"\nThis is same as pdc, but for LCD-via CRT1. Hence, this is for the 315/330/340\n"
	  "series only. (default: autodetected if LCD is in LCD-via-CRT1 mode during\n"
	  "startup) - Note: currently, this has no effect because LCD-via-CRT1 is not\n"
	  "implemented yet.\n");
#endif

MODULE_PARM_DESC(specialtiming,
	"\nPlease refer to documentation for more information on this option.\n");

MODULE_PARM_DESC(lvdshl,
	"\nPlease refer to documentation for more information on this option.\n");

MODULE_PARM_DESC(tvstandard,
	"\nThis allows overriding the BIOS default for the TV standard. Valid choices are\n"
	  "pal, ntsc, palm and paln. (default: [auto; pal or ntsc only])\n");

MODULE_PARM_DESC(tvxposoffset,
	"\nRelocate TV output horizontally. Possible parameters: -32 through 32.\n"
	  "Default: 0\n");

MODULE_PARM_DESC(tvyposoffset,
	"\nRelocate TV output vertically. Possible parameters: -32 through 32.\n"
	  "Default: 0\n");

MODULE_PARM_DESC(nocrt2rate,
	"\nSetting this to 1 will force the driver to use the default refresh rate for\n"
	  "CRT2 if CRT2 type is VGA. (default: 0, use same rate as CRT1)\n");

#if !defined(__i386__) && !defined(__x86_64__)
#ifdef CONFIG_FB_SIS_300
MODULE_PARM_DESC(resetcard,
	"\nSet this to 1 in order to reset (POST) the card on non-x86 machines where\n"
	  "the BIOS did not POST the card (only supported for SiS 300/305 and XGI cards\n"
	  "currently). Default: 0\n");

MODULE_PARM_DESC(videoram,
	"\nSet this to the amount of video RAM (in kilobyte) the card has. Required on\n"
	  "some non-x86 architectures where the memory auto detection fails. Only\n"
	  "relevant if resetcard is set, too. SiS300/305 only. Default: [auto-detect]\n");
#endif
#endif

#endif 	   /*  /MODULE  */

/* _GPL only for new symbols. */
EXPORT_SYMBOL(sis_malloc);
EXPORT_SYMBOL(sis_free);
EXPORT_SYMBOL_GPL(sis_malloc_new);
EXPORT_SYMBOL_GPL(sis_free_new);



