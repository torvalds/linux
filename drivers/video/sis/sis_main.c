/*
 * SiS 300/305/540/630(S)/730(S)
 * SiS 315(H/PRO)/55x/(M)65x/(M)661(F/M)X/740/741(GX)/330/(M)760
 * frame buffer driver for Linux kernels >= 2.4.14 and >=2.6.3
 *
 * Copyright (C) 2001-2004 Thomas Winischhofer, Vienna, Austria.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:   	Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Author of (practically wiped) code base:
 *		SiS (www.sis.com)
 *	 	Copyright (C) 1999 Silicon Integrated Systems, Inc.
 *
 * See http://www.winischhofer.net/ for more information and updates
 *
 * Originally based on the VBE 2.0 compliant graphic boards framebuffer driver,
 * which is (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#include <linux/moduleparam.h>
#endif
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/smp_lock.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/vt_kern.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#endif

#include "sis.h"
#include "sis_main.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,3)
#error "This version of sisfb requires at least 2.6.3"
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#ifdef FBCON_HAS_CFB8
extern struct display_switch fbcon_sis8;
#endif
#ifdef FBCON_HAS_CFB16
extern struct display_switch fbcon_sis16;
#endif
#ifdef FBCON_HAS_CFB32
extern struct display_switch fbcon_sis32;
#endif
#endif

/* ------------------ Internal helper routines ----------------- */

static void __init
sisfb_setdefaultparms(void)
{
	sisfb_off 		= 0;
	sisfb_parm_mem 		= 0;
	sisfb_accel 		= -1;
	sisfb_ypan      	= -1;
	sisfb_max 		= -1;
	sisfb_userom    	= -1;
        sisfb_useoem    	= -1;
#ifdef MODULE
	/* Module: "None" for 2.4, default mode for 2.5+ */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	sisfb_mode_idx 		= -1;
#else
	sisfb_mode_idx  	= MODE_INDEX_NONE;
#endif
#else
	/* Static: Default mode */
	sisfb_mode_idx  	= -1;
#endif
	sisfb_parm_rate 	= -1;
	sisfb_crt1off 		= 0;
	sisfb_forcecrt1 	= -1;
	sisfb_crt2type  	= -1;
	sisfb_crt2flags 	= 0;
	sisfb_pdc 		= 0xff;
	sisfb_pdca 		= 0xff;
	sisfb_scalelcd  	= -1;
	sisfb_specialtiming 	= CUT_NONE;
	sisfb_lvdshl 		= -1;
	sisfb_dstn     		= 0;
	sisfb_fstn 		= 0;
	sisfb_tvplug    	= -1;
	sisfb_tvstd     	= -1;
	sisfb_tvxposoffset 	= 0;
	sisfb_tvyposoffset 	= 0;
	sisfb_filter 		= -1;
	sisfb_nocrt2rate 	= 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	sisfb_inverse   	= 0;
	sisfb_fontname[0] 	= 0;
#endif
#if !defined(__i386__) && !defined(__x86_64__)
	sisfb_resetcard 	= 0;
	sisfb_videoram  	= 0;
#endif
}

static void __devinit
sisfb_search_vesamode(unsigned int vesamode, BOOLEAN quiet)
{
	int i = 0, j = 0;

	/* BEWARE: We don't know the hardware specs yet and there is no ivideo */

	if(vesamode == 0) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
		sisfb_mode_idx = MODE_INDEX_NONE;
#else
		if(!quiet) {
		   printk(KERN_ERR "sisfb: Invalid mode. Using default.\n");
		}
		sisfb_mode_idx = DEFAULT_MODE;
#endif
		return;
	}

	vesamode &= 0x1dff;  /* Clean VESA mode number from other flags */

	while(sisbios_mode[i++].mode_no[0] != 0) {
		if( (sisbios_mode[i-1].vesa_mode_no_1 == vesamode) ||
		    (sisbios_mode[i-1].vesa_mode_no_2 == vesamode) ) {
		    if(sisfb_fstn) {
		       if(sisbios_mode[i-1].mode_no[1] == 0x50 ||
		          sisbios_mode[i-1].mode_no[1] == 0x56 ||
		          sisbios_mode[i-1].mode_no[1] == 0x53) continue;
	            } else {
		       if(sisbios_mode[i-1].mode_no[1] == 0x5a ||
		          sisbios_mode[i-1].mode_no[1] == 0x5b) continue;
		    }
		    sisfb_mode_idx = i - 1;
		    j = 1;
		    break;
		}
	}
	if((!j) && !quiet) printk(KERN_ERR "sisfb: Invalid VESA mode 0x%x'\n", vesamode);
}

static void
sisfb_search_mode(char *name, BOOLEAN quiet)
{
	int i = 0;
	unsigned int j = 0, xres = 0, yres = 0, depth = 0, rate = 0;
	char strbuf[16], strbuf1[20];
	char *nameptr = name;

	/* BEWARE: We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL) {
	   if(!quiet) {
	      printk(KERN_ERR "sisfb: Internal error, using default mode.\n");
	   }
	   sisfb_mode_idx = DEFAULT_MODE;
	   return;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
        if(!strnicmp(name, sisbios_mode[MODE_INDEX_NONE].name, strlen(name))) {
	   if(!quiet) {
	      printk(KERN_ERR "sisfb: Mode 'none' not supported anymore. Using default.\n");
	   }
	   sisfb_mode_idx = DEFAULT_MODE;
	   return;
	}
#endif
	if(strlen(name) <= 19) {
	   strcpy(strbuf1, name);
	   for(i=0; i<strlen(strbuf1); i++) {
	      if(strbuf1[i] < '0' || strbuf1[i] > '9') strbuf1[i] = ' ';
	   }

	   /* This does some fuzzy mode naming detection */
	   if(sscanf(strbuf1, "%u %u %u %u", &xres, &yres, &depth, &rate) == 4) {
	      if((rate <= 32) || (depth > 32)) {
	         j = rate; rate = depth; depth = j;
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
		if(!strnicmp(nameptr, sisbios_mode[i++].name, strlen(nameptr))) {
		   	if(sisfb_fstn) {
		      		if(sisbios_mode[i-1].mode_no[1] == 0x50 ||
		         	   sisbios_mode[i-1].mode_no[1] == 0x56 ||
		         	   sisbios_mode[i-1].mode_no[1] == 0x53) continue;
	           	} else {
		      		if(sisbios_mode[i-1].mode_no[1] == 0x5a ||
		         	   sisbios_mode[i-1].mode_no[1] == 0x5b) continue;
		   	}
		   	sisfb_mode_idx = i - 1;
		   	j = 1;
		   	break;
		}
	}
	if((!j) && !quiet) printk(KERN_ERR "sisfb: Invalid mode '%s'\n", nameptr);
}

#ifndef MODULE
static void __devinit
sisfb_get_vga_mode_from_kernel(void)
{
#if (defined(__i386__) || defined(__x86_64__)) && defined(CONFIG_VIDEO_SELECT)
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

	    printk(KERN_DEBUG "sisfb: Using vga mode %s pre-set by kernel as default\n", mymode);

	    sisfb_search_mode(mymode, TRUE);
	}
#endif
	return;
}
#endif

static void __init
sisfb_search_crt2type(const char *name)
{
	int i = 0;

	/* BEWARE: We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL) return;

	while(sis_crt2type[i].type_no != -1) {
	   	if(!strnicmp(name, sis_crt2type[i].name, strlen(sis_crt2type[i].name))) {
	      		sisfb_crt2type = sis_crt2type[i].type_no;
	      		sisfb_tvplug = sis_crt2type[i].tvplug_no;
	      		sisfb_crt2flags = sis_crt2type[i].flags;
	      		break;
	   	}
	   	i++;
	}

	sisfb_dstn = (sisfb_crt2flags & FL_550_DSTN) ? 1 : 0;
	sisfb_fstn = (sisfb_crt2flags & FL_550_FSTN) ? 1 : 0;

	if(sisfb_crt2type < 0) {
		printk(KERN_ERR "sisfb: Invalid CRT2 type: %s\n", name);
	}
}

static void __init
sisfb_search_tvstd(const char *name)
{
	int i = 0;

	/* BEWARE: We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL) return;

	while(sis_tvtype[i].type_no != -1) {
	   	if(!strnicmp(name, sis_tvtype[i].name, strlen(sis_tvtype[i].name))) {
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
	BOOLEAN found = FALSE;

	/* BEWARE: We don't know the hardware specs yet and there is no ivideo */

	if(name == NULL) return;

	if(!strnicmp(name, "none", 4)) {
	        sisfb_specialtiming = CUT_FORCENONE;
		printk(KERN_DEBUG "sisfb: Special timing disabled\n");
	} else {
	   while(mycustomttable[i].chipID != 0) {
	      if(!strnicmp(name,mycustomttable[i].optionName, strlen(mycustomttable[i].optionName))) {
		 sisfb_specialtiming = mycustomttable[i].SpecialID;
		 found = TRUE;
		 printk(KERN_INFO "sisfb: Special timing for %s %s forced (\"%s\")\n",
		 	mycustomttable[i].vendorName, mycustomttable[i].cardName,
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

static BOOLEAN __devinit
sisfb_interpret_edid(struct sisfb_monitor *monitor, u8 *buffer)
{
	int i, j, xres, yres, refresh, index;
	u32 emodes;

	if(buffer[0] != 0x00 || buffer[1] != 0xff ||
	   buffer[2] != 0xff || buffer[3] != 0xff ||
	   buffer[4] != 0xff || buffer[5] != 0xff ||
	   buffer[6] != 0xff || buffer[7] != 0x00) {
	   printk(KERN_DEBUG "sisfb: Bad EDID header\n");
	   return FALSE;
	}

	if(buffer[0x12] != 0x01) {
	   printk(KERN_INFO "sisfb: EDID version %d not supported\n",
	   	buffer[0x12]);
	   return FALSE;
	}

	monitor->feature = buffer[0x18];

	if(!buffer[0x14] & 0x80) {
	   if(!(buffer[0x14] & 0x08)) {
	      printk(KERN_INFO "sisfb: WARNING: Monitor does not support separate syncs\n");
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
		  monitor->datavalid = TRUE;
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
		      if(monitor->dclockmax < sisfb_ddcsmodes[j].d) monitor->dclockmax = sisfb_ddcsmodes[i].d;
	            }
	         }
	      }
	      index += 2;
           }
	   if((monitor->hmin <= monitor->hmax) && (monitor->vmin <= monitor->vmax)) {
	      monitor->datavalid = TRUE;
	   }
	}

 	return(monitor->datavalid);
}

static void __devinit
sisfb_handle_ddc(struct sis_video_info *ivideo, struct sisfb_monitor *monitor, int crtno)
{
	USHORT  temp, i, realcrtno = crtno;
   	u8      buffer[256];

	monitor->datavalid = FALSE;

	if(crtno) {
       	   if(ivideo->vbflags & CRT2_LCD)      realcrtno = 1;
      	   else if(ivideo->vbflags & CRT2_VGA) realcrtno = 2;
      	   else return;
   	}

	if((ivideo->sisfb_crt1off) && (!crtno)) return;

    	temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags, ivideo->sisvga_engine,
				realcrtno, 0, &buffer[0]);
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
				     realcrtno, 1, &buffer[0]);
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

static BOOLEAN
sisfb_verify_rate(struct sis_video_info *ivideo, struct sisfb_monitor *monitor,
		int mode_idx, int rate_idx, int rate)
{
	int htotal, vtotal;
	unsigned int dclock, hsync;

	if(!monitor->datavalid) return TRUE;

	if(mode_idx < 0) return FALSE;

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
    		return TRUE;
#ifdef CONFIG_FB_SIS_315
	case 0x5a:
	case 0x5b:
		if(ivideo->sisvga_engine == SIS_315_VGA) return TRUE;
#endif
    	}

	if(rate < (monitor->vmin - 1)) return FALSE;
	if(rate > (monitor->vmax + 1)) return FALSE;

	if(sisfb_gettotalfrommode(&ivideo->SiS_Pr, &ivideo->sishw_ext,
				  sisbios_mode[mode_idx].mode_no[ivideo->mni],
	                          &htotal, &vtotal, rate_idx)) {
		dclock = (htotal * vtotal * rate) / 1000;
		if(dclock > (monitor->dclockmax + 1000)) return FALSE;
		hsync = dclock / htotal;
		if(hsync < (monitor->hmin - 1)) return FALSE;
		if(hsync > (monitor->hmax + 1)) return FALSE;
        } else {
	  	return FALSE;
	}
	return TRUE;
}

static int
sisfb_validate_mode(struct sis_video_info *ivideo, int myindex, u32 vbflags)
{
   u16 xres=0, yres, myres;

#ifdef CONFIG_FB_SIS_300
   if(ivideo->sisvga_engine == SIS_300_VGA) {
      if(!(sisbios_mode[myindex].chipset & MD_SIS300)) return(-1);
   }
#endif
#ifdef CONFIG_FB_SIS_315
   if(ivideo->sisvga_engine == SIS_315_VGA) {
      if(!(sisbios_mode[myindex].chipset & MD_SIS315)) return(-1);
   }
#endif

   myres = sisbios_mode[myindex].yres;

   switch(vbflags & VB_DISPTYPE_DISP2) {

     case CRT2_LCD:

        xres = ivideo->lcdxres; yres = ivideo->lcdyres;

	if(ivideo->SiS_Pr.SiS_CustomT != CUT_PANEL848) {
	   	if(sisbios_mode[myindex].xres > xres) return(-1);
           	if(myres > yres) return(-1);
	}

	if(vbflags & (VB_LVDS | VB_30xBDH)) {
	   if(sisbios_mode[myindex].xres == 320) {
	      if((myres == 240) || (myres == 480)) {
		 if(!ivideo->sisfb_fstn) {
		    if(sisbios_mode[myindex].mode_no[1] == 0x5a ||
		       sisbios_mode[myindex].mode_no[1] == 0x5b)
		       return(-1);
		 } else {
		    if(sisbios_mode[myindex].mode_no[1] == 0x50 ||
		       sisbios_mode[myindex].mode_no[1] == 0x56 ||
		       sisbios_mode[myindex].mode_no[1] == 0x53)
		       return(-1);
		 }
	      }
	   }
	}

	if(SiS_GetModeID_LCD(ivideo->sisvga_engine, vbflags, sisbios_mode[myindex].xres,
			     sisbios_mode[myindex].yres, 0, ivideo->sisfb_fstn,
			     ivideo->SiS_Pr.SiS_CustomT, xres, yres) < 0x14) {
	   	return(-1);
	}
	break;

     case CRT2_TV:
	if(SiS_GetModeID_TV(ivideo->sisvga_engine, vbflags, sisbios_mode[myindex].xres,
	                    sisbios_mode[myindex].yres, 0) < 0x14) {
	   	return(-1);
	}
	break;

     case CRT2_VGA:
        if(SiS_GetModeID_VGA2(ivideo->sisvga_engine, vbflags, sisbios_mode[myindex].xres,
	                      sisbios_mode[myindex].yres, 0) < 0x14) {
	   	return(-1);
	}
	break;
     }

     return(myindex);
}

static u8
sisfb_search_refresh_rate(struct sis_video_info *ivideo, unsigned int rate, int mode_idx)
{
	u16 xres, yres;
	int i = 0;

	xres = sisbios_mode[mode_idx].xres;
	yres = sisbios_mode[mode_idx].yres;

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
				} else if(((rate - sisfb_vrate[i-1].refresh) <= 2)
						&& (sisfb_vrate[i].idx != 1)) {
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

static BOOLEAN
sisfb_bridgeisslave(struct sis_video_info *ivideo)
{
   unsigned char P1_00;

   if(!(ivideo->vbflags & VB_VIDEOBRIDGE)) return FALSE;

   inSISIDXREG(SISPART1,0x00,P1_00);
   if( ((ivideo->sisvga_engine == SIS_300_VGA) && (P1_00 & 0xa0) == 0x20) ||
       ((ivideo->sisvga_engine == SIS_315_VGA) && (P1_00 & 0x50) == 0x10) ) {
	   return TRUE;
   } else {
           return FALSE;
   }
}

static BOOLEAN
sisfballowretracecrt1(struct sis_video_info *ivideo)
{
   u8 temp;

   inSISIDXREG(SISCR,0x17,temp);
   if(!(temp & 0x80)) return FALSE;

   inSISIDXREG(SISSR,0x1f,temp);
   if(temp & 0xc0) return FALSE;

   return TRUE;
}

static BOOLEAN
sisfbcheckvretracecrt1(struct sis_video_info *ivideo)
{
   if(!sisfballowretracecrt1(ivideo)) return FALSE;

   if(inSISREG(SISINPSTAT) & 0x08) return TRUE;
   else 			   return FALSE;
}

static void
sisfbwaitretracecrt1(struct sis_video_info *ivideo)
{
   int watchdog;

   if(!sisfballowretracecrt1(ivideo)) return;

   watchdog = 65536;
   while((!(inSISREG(SISINPSTAT) & 0x08)) && --watchdog);
   watchdog = 65536;
   while((inSISREG(SISINPSTAT) & 0x08) && --watchdog);
}

static BOOLEAN
sisfbcheckvretracecrt2(struct sis_video_info *ivideo)
{
   unsigned char temp, reg;

   switch(ivideo->sisvga_engine) {
   case SIS_300_VGA: reg = 0x25; break;
   case SIS_315_VGA: reg = 0x30; break;
   default:          return FALSE;
   }

   inSISIDXREG(SISPART1, reg, temp);
   if(temp & 0x02) return TRUE;
   else 	   return FALSE;
}

static BOOLEAN
sisfb_CheckVBRetrace(struct sis_video_info *ivideo)
{
   if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
      if(sisfb_bridgeisslave(ivideo)) {
         return(sisfbcheckvretracecrt1(ivideo));
      } else {
         return(sisfbcheckvretracecrt2(ivideo));
      }
   } 
   return(sisfbcheckvretracecrt1(ivideo));
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
      inSISIDXREG(SISPART1,(idx+0),reg1); /* 30 */
      inSISIDXREG(SISPART1,(idx+1),reg2); /* 31 */
      inSISIDXREG(SISPART1,(idx+2),reg3); /* 32 */
      inSISIDXREG(SISPART1,(idx+3),reg4); /* 33 */
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
      reg1 = inSISREG(SISINPSTAT);
      if(reg1 & 0x08) ret |= FB_VBLANK_VSYNCING;
      if(reg1 & 0x01) ret |= FB_VBLANK_VBLANKING;
      inSISIDXREG(SISCR,0x20,reg1);
      inSISIDXREG(SISCR,0x1b,reg1);
      inSISIDXREG(SISCR,0x1c,reg2);
      inSISIDXREG(SISCR,0x1d,reg3);
      (*vcount) = reg2 | ((reg3 & 0x07) << 8);
      (*hcount) = (reg1 | ((reg3 & 0x10) << 4)) << 3;
   }
   return ret;
}

static int
sisfb_myblank(struct sis_video_info *ivideo, int blank)
{
   u8 sr01, sr11, sr1f, cr63=0, p2_0, p1_13;
   BOOLEAN backlight = TRUE;

   switch(blank) {
   case FB_BLANK_UNBLANK:	/* on */
      sr01  = 0x00;
      sr11  = 0x00;
      sr1f  = 0x00;
      cr63  = 0x00;
      p2_0  = 0x20;
      p1_13 = 0x00;
      backlight = TRUE;
      break;
   case FB_BLANK_NORMAL:	/* blank */
      sr01  = 0x20;
      sr11  = 0x00;
      sr1f  = 0x00;
      cr63  = 0x00;
      p2_0  = 0x20;
      p1_13 = 0x00;
      backlight = TRUE;
      break;
   case FB_BLANK_VSYNC_SUSPEND:	/* no vsync */
      sr01  = 0x20;
      sr11  = 0x08;
      sr1f  = 0x80;
      cr63  = 0x40;
      p2_0  = 0x40;
      p1_13 = 0x80;
      backlight = FALSE;
      break;
   case FB_BLANK_HSYNC_SUSPEND:	/* no hsync */
      sr01  = 0x20;
      sr11  = 0x08;
      sr1f  = 0x40;
      cr63  = 0x40;
      p2_0  = 0x80;
      p1_13 = 0x40;
      backlight = FALSE;
      break;
   case FB_BLANK_POWERDOWN:	/* off */
      sr01  = 0x20;
      sr11  = 0x08;
      sr1f  = 0xc0;
      cr63  = 0x40;
      p2_0  = 0xc0;
      p1_13 = 0xc0;
      backlight = FALSE;
      break;
   default:
      return 1;
   }

   if(ivideo->currentvbflags & VB_DISPTYPE_CRT1) {

      if( (!ivideo->sisfb_thismonitor.datavalid) ||
          ((ivideo->sisfb_thismonitor.datavalid) &&
           (ivideo->sisfb_thismonitor.feature & 0xe0))) {

	 if(ivideo->sisvga_engine == SIS_315_VGA) {
	    setSISIDXREG(SISCR, ivideo->SiS_Pr.SiS_MyCR63, 0xbf, cr63);
	 }

	 if(!(sisfb_bridgeisslave(ivideo))) {
	    setSISIDXREG(SISSR, 0x01, ~0x20, sr01);
	    setSISIDXREG(SISSR, 0x1f, 0x3f, sr1f);
	 }
      }

   }

   if(ivideo->currentvbflags & CRT2_LCD) {

      if(ivideo->vbflags & (VB_301LV|VB_302LV|VB_302ELV)) {
	 if(backlight) {
	    SiS_SiS30xBLOn(&ivideo->SiS_Pr, &ivideo->sishw_ext);
	 } else {
	    SiS_SiS30xBLOff(&ivideo->SiS_Pr, &ivideo->sishw_ext);
	 }
      } else if(ivideo->sisvga_engine == SIS_315_VGA) {
	 if(ivideo->vbflags & VB_CHRONTEL) {
	    if(backlight) {
	       SiS_Chrontel701xBLOn(&ivideo->SiS_Pr,&ivideo->sishw_ext);
	    } else {
	       SiS_Chrontel701xBLOff(&ivideo->SiS_Pr);
	    }
	 }
      }

      if(((ivideo->sisvga_engine == SIS_300_VGA) &&
          (ivideo->vbflags & (VB_301|VB_30xBDH|VB_LVDS))) ||
         ((ivideo->sisvga_engine == SIS_315_VGA) &&
          ((ivideo->vbflags & (VB_LVDS | VB_CHRONTEL)) == VB_LVDS))) {
          setSISIDXREG(SISSR, 0x11, ~0x0c, sr11);
      }

      if(ivideo->sisvga_engine == SIS_300_VGA) {
         if((ivideo->vbflags & (VB_301B|VB_301C|VB_302B)) &&
            (!(ivideo->vbflags & VB_30xBDH))) {
	    setSISIDXREG(SISPART1, 0x13, 0x3f, p1_13);
	 }
      } else if(ivideo->sisvga_engine == SIS_315_VGA) {
         if((ivideo->vbflags & (VB_301B|VB_301C|VB_302B)) &&
            (!(ivideo->vbflags & VB_30xBDH))) {
	    setSISIDXREG(SISPART2, 0x00, 0x1f, p2_0);
	 }
      }

   } else if(ivideo->currentvbflags & CRT2_VGA) {

      if(ivideo->vbflags & (VB_301B|VB_301C|VB_302B)) {
         setSISIDXREG(SISPART2, 0x00, 0x1f, p2_0);
      }

   }

   return(0);
}

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
		break;
   	}
}

static int
sisfb_calc_maxyres(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	int maxyres = ivideo->heapstart / (var->xres_virtual * (var->bits_per_pixel >> 3));

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
   	BOOLEAN isslavemode = FALSE;
	unsigned short HDisplay1 = ivideo->scrnpitchCRT1 >> 3;
	unsigned short HDisplay2 = ivideo->video_linelength >> 3;

   	if(sisfb_bridgeisslave(ivideo)) isslavemode = TRUE;

   	/* We need to set pitch for CRT1 if bridge is in slave mode, too */
   	if((ivideo->currentvbflags & VB_DISPTYPE_DISP1) || (isslavemode)) {
   		outSISIDXREG(SISCR,0x13,(HDisplay1 & 0xFF));
   		setSISIDXREG(SISSR,0x0E,0xF0,(HDisplay1 >> 8));
	}

   	/* We must not set the pitch for CRT2 if bridge is in slave mode */
   	if((ivideo->currentvbflags & VB_DISPTYPE_DISP2) && (!isslavemode)) {
		orSISIDXREG(SISPART1,ivideo->CRT2_write_enable,0x01);
   		outSISIDXREG(SISPART1,0x07,(HDisplay2 & 0xFF));
   		setSISIDXREG(SISPART1,0x09,0xF0,(HDisplay2 >> 8));
   	}
}

static void
sisfb_bpp_to_var(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	ivideo->video_cmap_len = sisfb_get_cmap_len(var);

	switch(var->bits_per_pixel) {
	case 8:
		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 6;
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
sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	unsigned int htotal = 0, vtotal = 0;
	unsigned int drate = 0, hrate = 0;
	int found_mode = 0;
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

	if(sisfb_search_refresh_rate(ivideo, ivideo->refresh_rate, ivideo->sisfb_mode_idx) == 0) {
		ivideo->rate_idx = sisbios_mode[ivideo->sisfb_mode_idx].rate_idx;
		ivideo->refresh_rate = 60;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	if(ivideo->sisfb_thismonitor.datavalid) {
	   if(!sisfb_verify_rate(ivideo, &ivideo->sisfb_thismonitor, ivideo->sisfb_mode_idx,
	                         ivideo->rate_idx, ivideo->refresh_rate)) {
	      printk(KERN_INFO "sisfb: WARNING: Refresh rate exceeds monitor specs!\n");
	   }
	}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	if(((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive) {
#else
	if(isactive) {
#endif
		sisfb_pre_setmode(ivideo);

		if(SiSSetMode(&ivideo->SiS_Pr, &ivideo->sishw_ext, ivideo->mode_no) == 0) {
			printk(KERN_ERR "sisfb: Setting mode[0x%x] failed\n", ivideo->mode_no);
			return -EINVAL;
		}

		outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

		sisfb_post_setmode(ivideo);

		ivideo->video_bpp    = sisbios_mode[ivideo->sisfb_mode_idx].bpp;
		ivideo->video_width  = sisbios_mode[ivideo->sisfb_mode_idx].xres;
		ivideo->video_height = sisbios_mode[ivideo->sisfb_mode_idx].yres;

		sisfb_calc_pitch(ivideo, var);
		sisfb_set_pitch(ivideo);

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

		sisfb_set_vparms(ivideo);

		ivideo->current_width = ivideo->video_width;
		ivideo->current_height = ivideo->video_height;
		ivideo->current_bpp = ivideo->video_bpp;
		ivideo->current_htotal = htotal;
		ivideo->current_vtotal = vtotal;
		ivideo->current_linelength = ivideo->video_linelength;
		ivideo->current_pixclock = var->pixclock;
		ivideo->current_refresh_rate = ivideo->refresh_rate;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
                ivideo->sisfb_lastrates[ivideo->mode_no] = ivideo->refresh_rate;
#endif
	}

	return 0;
}

static int
sisfb_pan_var(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	unsigned int base;

	if(var->xoffset > (var->xres_virtual - var->xres)) {
		return -EINVAL;
	}
	if(var->yoffset > (var->yres_virtual - var->yres)) {
		return -EINVAL;
	}

	base = (var->yoffset * var->xres_virtual) + var->xoffset;

        /* calculate base bpp dep. */
        switch(var->bits_per_pixel) {
	case 32:
            	break;
        case 16:
        	base >>= 1;
        	break;
	case 8:
        default:
        	base >>= 2;
            	break;
        }
	
	outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

        outSISIDXREG(SISCR, 0x0D, base & 0xFF);
	outSISIDXREG(SISCR, 0x0C, (base >> 8) & 0xFF);
	outSISIDXREG(SISSR, 0x0D, (base >> 16) & 0xFF);
	if(ivideo->sisvga_engine == SIS_315_VGA) {
		setSISIDXREG(SISSR, 0x37, 0xFE, (base >> 24) & 0x01);
	}
        if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
		orSISIDXREG(SISPART1, ivideo->CRT2_write_enable, 0x01);
        	outSISIDXREG(SISPART1, 0x06, (base & 0xFF));
        	outSISIDXREG(SISPART1, 0x05, ((base >> 8) & 0xFF));
        	outSISIDXREG(SISPART1, 0x04, ((base >> 16) & 0xFF));
		if(ivideo->sisvga_engine == SIS_315_VGA) {
			setSISIDXREG(SISPART1, 0x02, 0x7F, ((base >> 24) & 0x01) << 7);
		}
        }
	return 0;
}

/* ------------ FBDev related routines for 2.4 series ----------- */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

static void
sisfb_crtc_to_var(struct sis_video_info *ivideo, struct fb_var_screeninfo *var)
{
	u16 VRE, VBE, VRS, VBS, VDE, VT;
	u16 HRE, HBE, HRS, HBS, HDE, HT;
	u8  sr_data, cr_data, cr_data2, cr_data3, mr_data;
	int A, B, C, D, E, F, temp;
	unsigned int hrate, drate, maxyres;

	inSISIDXREG(SISSR, IND_SIS_COLOR_MODE, sr_data);

	if(sr_data & SIS_INTERLACED_MODE)
	   var->vmode = FB_VMODE_INTERLACED;
	else
	   var->vmode = FB_VMODE_NONINTERLACED;

	switch((sr_data & 0x1C) >> 2) {
	case SIS_8BPP_COLOR_MODE:
		var->bits_per_pixel = 8;
		break;
	case SIS_16BPP_COLOR_MODE:
		var->bits_per_pixel = 16;
		break;
	case SIS_32BPP_COLOR_MODE:
		var->bits_per_pixel = 32;
		break;
	}

	sisfb_bpp_to_var(ivideo, var);
	
	inSISIDXREG(SISSR, 0x0A, sr_data);
        inSISIDXREG(SISCR, 0x06, cr_data);
        inSISIDXREG(SISCR, 0x07, cr_data2);

	VT = (cr_data & 0xFF) |
	     ((u16) (cr_data2 & 0x01) << 8) |
	     ((u16) (cr_data2 & 0x20) << 4) |
	     ((u16) (sr_data  & 0x01) << 10);
	A = VT + 2;

	inSISIDXREG(SISCR, 0x12, cr_data);

	VDE = (cr_data & 0xff) |
	      ((u16) (cr_data2 & 0x02) << 7) |
	      ((u16) (cr_data2 & 0x40) << 3) |
	      ((u16) (sr_data  & 0x02) << 9);
	E = VDE + 1;

	inSISIDXREG(SISCR, 0x10, cr_data);

	VRS = (cr_data & 0xff) |
	      ((u16) (cr_data2 & 0x04) << 6) |
	      ((u16) (cr_data2 & 0x80) << 2) |
	      ((u16) (sr_data  & 0x08) << 7);
	F = VRS + 1 - E;

	inSISIDXREG(SISCR, 0x15, cr_data);
	inSISIDXREG(SISCR, 0x09, cr_data3);

	if(cr_data3 & 0x80) var->vmode = FB_VMODE_DOUBLE;

	VBS = (cr_data & 0xff) |
	      ((u16) (cr_data2 & 0x08) << 5) |
	      ((u16) (cr_data3 & 0x20) << 4) |
	      ((u16) (sr_data & 0x04) << 8);

	inSISIDXREG(SISCR, 0x16, cr_data);

	VBE = (cr_data & 0xff) | ((u16) (sr_data & 0x10) << 4);
	temp = VBE - ((E - 1) & 511);
	B = (temp > 0) ? temp : (temp + 512);

	inSISIDXREG(SISCR, 0x11, cr_data);

	VRE = (cr_data & 0x0f) | ((sr_data & 0x20) >> 1);
	temp = VRE - ((E + F - 1) & 31);
	C = (temp > 0) ? temp : (temp + 32);

	D = B - F - C;

        var->yres = E;
	var->upper_margin = D;
	var->lower_margin = F;
	var->vsync_len = C;

	if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
	   var->yres <<= 1;
	   var->upper_margin <<= 1;
	   var->lower_margin <<= 1;
	   var->vsync_len <<= 1;
	} else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
	   var->yres >>= 1;
	   var->upper_margin >>= 1;
	   var->lower_margin >>= 1;
	   var->vsync_len >>= 1;
	}

	inSISIDXREG(SISSR, 0x0b, sr_data);
	inSISIDXREG(SISCR, 0x00, cr_data);

	HT = (cr_data & 0xff) | ((u16) (sr_data & 0x03) << 8);
	A = HT + 5;

	inSISIDXREG(SISCR, 0x01, cr_data);

	HDE = (cr_data & 0xff) | ((u16) (sr_data & 0x0C) << 6);
	E = HDE + 1;

	inSISIDXREG(SISCR, 0x04, cr_data);

	HRS = (cr_data & 0xff) | ((u16) (sr_data & 0xC0) << 2);
	F = HRS - E - 3;

	inSISIDXREG(SISCR, 0x02, cr_data);

	HBS = (cr_data & 0xff) | ((u16) (sr_data & 0x30) << 4);

	inSISIDXREG(SISSR, 0x0c, sr_data);
	inSISIDXREG(SISCR, 0x03, cr_data);
	inSISIDXREG(SISCR, 0x05, cr_data2);

	HBE = (cr_data & 0x1f) |
	      ((u16) (cr_data2 & 0x80) >> 2) |
	      ((u16) (sr_data  & 0x03) << 6);
	HRE = (cr_data2 & 0x1f) | ((sr_data & 0x04) << 3);

	temp = HBE - ((E - 1) & 255);
	B = (temp > 0) ? temp : (temp + 256);

	temp = HRE - ((E + F + 3) & 63);
	C = (temp > 0) ? temp : (temp + 64);

	D = B - F - C;

	var->xres = E * 8;
	if(var->xres_virtual < var->xres) {
		var->xres_virtual = var->xres;
	}

	if((var->xres == 320) &&
	   (var->yres == 200 || var->yres == 240)) {
		/* Terrible hack, but the correct CRTC data for
	  	 * these modes only produces a black screen...
	  	 */
       		var->left_margin = (400 - 376);
       		var->right_margin = (328 - 320);
       		var->hsync_len = (376 - 328);
	} else {
	   	var->left_margin = D * 8;
	   	var->right_margin = F * 8;
	   	var->hsync_len = C * 8;
	}
	var->activate = FB_ACTIVATE_NOW;

	var->sync = 0;

	mr_data = inSISREG(SISMISCR);
	if(mr_data & 0x80)
	   var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
	else
	   var->sync |= FB_SYNC_VERT_HIGH_ACT;

	if(mr_data & 0x40)
	   var->sync &= ~FB_SYNC_HOR_HIGH_ACT;
	else
	   var->sync |= FB_SYNC_HOR_HIGH_ACT;

	VT += 2;
	VT <<= 1;
	HT = (HT + 5) * 8;

	if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
	   VT <<= 1;
	}
	hrate = ivideo->refresh_rate * VT / 2;
	drate = (hrate * HT) / 1000;
	var->pixclock = (u32) (1000000000 / drate);

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
	   var->yres_virtual = var->yres;
	}

}

static int
sis_getcolreg(unsigned regno, unsigned *red, unsigned *green, unsigned *blue,
			 unsigned *transp, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	if(regno >= ivideo->video_cmap_len) return 1;

	*red   = ivideo->sis_palette[regno].red;
	*green = ivideo->sis_palette[regno].green;
	*blue  = ivideo->sis_palette[regno].blue;
	*transp = 0;

	return 0;
}

static int
sisfb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
                           unsigned transp, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	if(regno >= ivideo->video_cmap_len) return 1;

	ivideo->sis_palette[regno].red   = red;
	ivideo->sis_palette[regno].green = green;
	ivideo->sis_palette[regno].blue  = blue;

	switch(ivideo->video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:
	        outSISREG(SISDACA, regno);
		outSISREG(SISDACD, (red >> 10));
		outSISREG(SISDACD, (green >> 10));
		outSISREG(SISDACD, (blue >> 10));
		if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
		        outSISREG(SISDAC2A, regno);
			outSISREG(SISDAC2D, (red >> 8));
			outSISREG(SISDAC2D, (green >> 8));
			outSISREG(SISDAC2D, (blue >> 8));
		}
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		ivideo->sis_fbcon_cmap.cfb16[regno] =
		    ((red & 0xf800)) | ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		red   >>= 8;
		green >>= 8;
		blue  >>= 8;
		ivideo->sis_fbcon_cmap.cfb32[regno] = (red << 16) | (green << 8) | (blue);
		break;
#endif
	}

	return 0;
}

static void
sisfb_set_disp(int con, struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct sis_video_info    *ivideo = (struct sis_video_info *)info->par;
	struct display           *display;
	struct display_switch    *sw;
	struct fb_fix_screeninfo fix;
	long   flags;

	display = (con >= 0) ? &fb_display[con] : &ivideo->sis_disp;

	sisfb_get_fix(&fix, con, info);

	display->var = *var;
	display->screen_base = (char *)ivideo->video_vbase;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->can_soft_blank = 1;
	display->inverse = ivideo->sisfb_inverse;
	display->next_line = fix.line_length;

	save_flags(flags);

	switch(ivideo->video_bpp) {
#ifdef FBCON_HAS_CFB8
	case 8:	sw = ivideo->accel ? &fbcon_sis8 : &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:sw = ivideo->accel ? &fbcon_sis16 : &fbcon_cfb16;
		display->dispsw_data = &ivideo->sis_fbcon_cmap.cfb16;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:sw = ivideo->accel ? &fbcon_sis32 : &fbcon_cfb32;
		display->dispsw_data = &ivideo->sis_fbcon_cmap.cfb32;
		break;
#endif
	default:sw = &fbcon_dummy;
		break;
	}
	memcpy(&ivideo->sisfb_sw, sw, sizeof(*sw));
	display->dispsw = &ivideo->sisfb_sw;

	restore_flags(flags);

        if(ivideo->sisfb_ypan) {
  	    /* display->scrollmode = 0;  */
	} else {
	    display->scrollmode = SCROLL_YREDRAW;
	    ivideo->sisfb_sw.bmove = fbcon_redraw_bmove;
	}
}

static void
sisfb_do_install_cmap(int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

        if(con != ivideo->currcon) return;

        if(fb_display[con].cmap.len) {
		fb_set_cmap(&fb_display[con].cmap, 1, sisfb_setcolreg, info);
        } else {
		int size = sisfb_get_cmap_len(&fb_display[con].var);
		fb_set_cmap(fb_default_cmap(size), 1, sisfb_setcolreg, info);
	}
}

static int
sisfb_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	if(con == -1) {
		memcpy(var, &ivideo->default_var, sizeof(struct fb_var_screeninfo));
	} else {
		*var = fb_display[con].var;
	}

	if(ivideo->sisfb_fstn) {
	   	if(var->xres == 320 && var->yres == 480) var->yres = 240;
        }

	return 0;
}

static int
sisfb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	int err;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

        if(sisfb_do_set_var(var, con == ivideo->currcon, info)) {
		sisfb_crtc_to_var(ivideo, var);
		return -EINVAL;
	}

	sisfb_crtc_to_var(ivideo, var);

	sisfb_set_disp(con, var, info);

	if(info->changevar) {
		(*info->changevar)(con);
	}

	if((err = fb_alloc_cmap(&fb_display[con].cmap, 0, 0))) {
		return err;
	}

	sisfb_do_install_cmap(con, info);

#if 0	/* Why was this called here? */
	unsigned int cols, rows;
	cols = sisbios_mode[ivideo->sisfb_mode_idx].cols;
	rows = sisbios_mode[ivideo->sisfb_mode_idx].rows;
 	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);
#endif
	return 0;
}

static int
sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	struct display *display;

	display = (con >= 0) ? &fb_display[con] : &ivideo->sis_disp;

        if(con == ivideo->currcon) {

		return fb_get_cmap(cmap, kspc, sis_getcolreg, info);

	} else if(display->cmap.len) {

		fb_copy_cmap(&display->cmap, cmap, kspc ? 0 : 2);

	} else {

		int size = sisfb_get_cmap_len(&display->var);
		fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);

	}

	return 0;
}

static int
sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	struct display *display;
	int err, size;

	display = (con >= 0) ? &fb_display[con] : &ivideo->sis_disp;

	size = sisfb_get_cmap_len(&display->var);
	if(display->cmap.len != size) {
		err = fb_alloc_cmap(&display->cmap, size, 0);
		if(err)	return err;
	}
        
	if(con == ivideo->currcon) {
		return fb_set_cmap(cmap, kspc, sisfb_setcolreg, info);
	} else {
		fb_copy_cmap(cmap, &display->cmap, kspc ? 0 : 1);
	}

	return 0;
}

static int
sisfb_pan_display(struct fb_var_screeninfo *var, int con, struct fb_info* info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	int err;

	if(var->vmode & FB_VMODE_YWRAP) return -EINVAL;

	if((var->xoffset+fb_display[con].var.xres > fb_display[con].var.xres_virtual) ||
	   (var->yoffset+fb_display[con].var.yres > fb_display[con].var.yres_virtual)) {
		return -EINVAL;
	}

        if(con == ivideo->currcon) {
	   	if((err = sisfb_pan_var(ivideo, var)) < 0) return err;
	}

	fb_display[con].var.xoffset = var->xoffset;
	fb_display[con].var.yoffset = var->yoffset;

	return 0;
}

static int
sisfb_update_var(int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

        return(sisfb_pan_var(ivideo, &fb_display[con].var));
}

static int
sisfb_switch(int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	int cols, rows;

        if(fb_display[ivideo->currcon].cmap.len) {
		fb_get_cmap(&fb_display[ivideo->currcon].cmap, 1, sis_getcolreg, info);
	}

	fb_display[con].var.activate = FB_ACTIVATE_NOW;

	if(!memcmp(&fb_display[con].var, &fb_display[ivideo->currcon].var,
	                           	sizeof(struct fb_var_screeninfo))) {
		ivideo->currcon = con;
		return 1;
	}

	ivideo->currcon = con;

	sisfb_do_set_var(&fb_display[con].var, 1, info);

	sisfb_set_disp(con, &fb_display[con].var, info);

	sisfb_do_install_cmap(con, info);

	cols = sisbios_mode[ivideo->sisfb_mode_idx].cols;
	rows = sisbios_mode[ivideo->sisfb_mode_idx].rows;
	vc_resize_con(rows, cols, fb_display[con].conp->vc_num);

	sisfb_update_var(con, info);

	return 1;
}

static void
sisfb_blank(int blank, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	sisfb_myblank(ivideo, blank);
}
#endif

/* ------------ FBDev related routines for 2.6 series ----------- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)

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

	if(regno >= sisfb_get_cmap_len(&info->var)) return 1;

	switch(info->var.bits_per_pixel) {
	case 8:
	        outSISREG(SISDACA, regno);
		outSISREG(SISDACD, (red >> 10));
		outSISREG(SISDACD, (green >> 10));
		outSISREG(SISDACD, (blue >> 10));
		if(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {
		        outSISREG(SISDAC2A, regno);
			outSISREG(SISDAC2D, (red >> 8));
			outSISREG(SISDAC2D, (green >> 8));
			outSISREG(SISDAC2D, (blue >> 8));
		}
		break;
	case 16:
		((u32 *)(info->pseudo_palette))[regno] =
		    ((red & 0xf800)) | ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		break;
	case 32:
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

        if((err = sisfb_do_set_var(&info->var, 1, info))) {
		return err;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
	sisfb_get_fix(&info->fix, info->currcon, info);
#else
      	sisfb_get_fix(&info->fix, -1, info);
#endif
	return 0;
}

static int
sisfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	unsigned int htotal = 0, vtotal = 0, myrateindex = 0;
	unsigned int drate = 0, hrate = 0, maxyres;
	int found_mode = 0;
	int refresh_rate, search_idx;
	BOOLEAN recalc_clock = FALSE;
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
		SISFAIL("sisfb: no valid timing data");
	}

	search_idx = 0;
	while( (sisbios_mode[search_idx].mode_no[0] != 0) &&
	       (sisbios_mode[search_idx].xres <= var->xres) ) {
		if( (sisbios_mode[search_idx].xres == var->xres) &&
		    (sisbios_mode[search_idx].yres == var->yres) &&
		    (sisbios_mode[search_idx].bpp == var->bits_per_pixel)) {
		        if(sisfb_validate_mode(ivideo, search_idx, ivideo->currentvbflags) > 0) {
			   found_mode = 1;
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
		          if(sisfb_validate_mode(ivideo,search_idx, ivideo->currentvbflags) > 0) {
			     found_mode = 1;
			     break;
			  }
		   }
		   search_idx++;
	        }
		if(found_mode) {
			printk(KERN_DEBUG "sisfb: Adapted from %dx%dx%d to %dx%dx%d\n",
		   		var->xres, var->yres, var->bits_per_pixel,
				sisbios_mode[search_idx].xres,
				sisbios_mode[search_idx].yres,
				var->bits_per_pixel);
			var->xres = sisbios_mode[search_idx].xres;
		      	var->yres = sisbios_mode[search_idx].yres;


		} else {
		   	printk(KERN_ERR "sisfb: Failed to find supported mode near %dx%dx%d\n",
				var->xres, var->yres, var->bits_per_pixel);
		   	return -EINVAL;
		}
	}

	if( ((ivideo->vbflags & VB_LVDS) ||			/* Slave modes on LVDS and 301B-DH */
	     ((ivideo->vbflags & VB_30xBDH) && (ivideo->currentvbflags & CRT2_LCD))) &&
	    (var->bits_per_pixel == 8) ) {
	    	refresh_rate = 60;
		recalc_clock = TRUE;
	} else if( (ivideo->current_htotal == htotal) &&	/* x=x & y=y & c=c -> assume depth change */
	    	   (ivideo->current_vtotal == vtotal) &&
	    	   (ivideo->current_pixclock == pixclock) ) {
		drate = 1000000000 / pixclock;
	        hrate = (drate * 1000) / htotal;
	        refresh_rate = (unsigned int) (hrate * 2 / vtotal);
	} else if( ( (ivideo->current_htotal != htotal) ||	/* x!=x | y!=y & c=c -> invalid pixclock */
	    	     (ivideo->current_vtotal != vtotal) ) &&
	    	   (ivideo->current_pixclock == var->pixclock) ) {
		if(ivideo->sisfb_lastrates[sisbios_mode[search_idx].mode_no[ivideo->mni]]) {
			refresh_rate = ivideo->sisfb_lastrates[sisbios_mode[search_idx].mode_no[ivideo->mni]];
		} else if(ivideo->sisfb_parm_rate != -1) {
			/* Sic, sisfb_parm_rate - want to know originally desired rate here */
			refresh_rate = ivideo->sisfb_parm_rate;
		} else {
			refresh_rate = 60;
		}
		recalc_clock = TRUE;
	} else if((pixclock) && (htotal) && (vtotal)) {
		drate = 1000000000 / pixclock;
	   	hrate = (drate * 1000) / htotal;
	   	refresh_rate = (unsigned int) (hrate * 2 / vtotal);
	} else if(ivideo->current_refresh_rate) {
		refresh_rate = ivideo->current_refresh_rate;
		recalc_clock = TRUE;
	} else {
		refresh_rate = 60;
		recalc_clock = TRUE;
	}

	myrateindex = sisfb_search_refresh_rate(ivideo, refresh_rate, search_idx);

	/* Eventually recalculate timing and clock */
	if(recalc_clock) {
	   if(!myrateindex) myrateindex = sisbios_mode[search_idx].rate_idx;
	   var->pixclock = (u32) (1000000000 / sisfb_mode_rate_to_dclock(&ivideo->SiS_Pr,
	   					&ivideo->sishw_ext,
						sisbios_mode[search_idx].mode_no[ivideo->mni],
						myrateindex));
	   sisfb_mode_rate_to_ddata(&ivideo->SiS_Pr, &ivideo->sishw_ext,
		 		    sisbios_mode[search_idx].mode_no[ivideo->mni], myrateindex,	var);
	   if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
	      var->pixclock <<= 1;
	   }
	}

	if(ivideo->sisfb_thismonitor.datavalid) {
	   if(!sisfb_verify_rate(ivideo, &ivideo->sisfb_thismonitor, search_idx,
	                         myrateindex, refresh_rate)) {
	      printk(KERN_INFO "sisfb: WARNING: Refresh rate exceeds monitor specs!\n");
	   }
	}

	/* Adapt RGB settings */
	sisfb_bpp_to_var(ivideo, var);
	
	/* Sanity check for offsets */
	if(var->xoffset < 0) var->xoffset = 0;
	if(var->yoffset < 0) var->yoffset = 0;

	if(var->xres > var->xres_virtual) {
	   var->xres_virtual = var->xres;
	}

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

	if(var->xoffset > (var->xres_virtual - var->xres)) {
		return -EINVAL;
	}
	if(var->yoffset > (var->yres_virtual - var->yres)) {
		return -EINVAL;
	}

	if(var->vmode & FB_VMODE_YWRAP) return -EINVAL;

	if(var->xoffset + info->var.xres > info->var.xres_virtual ||
	   var->yoffset + info->var.yres > info->var.yres_virtual) {
		return -EINVAL;
	}

	if((err = sisfb_pan_var(ivideo, var)) < 0) return err;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	return 0;
}

static int
sisfb_blank(int blank, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	return(sisfb_myblank(ivideo, blank));
}

#endif

/* ----------- FBDev related routines for all series ---------- */

static int
sisfb_ioctl(struct inode *inode, struct file *file,
            unsigned int cmd, unsigned long arg,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	    int con,
#endif
	    struct fb_info *info)
{
	struct sis_video_info	*ivideo = (struct sis_video_info *)info->par;
	struct sis_memreq 	sismemreq;
	struct fb_vblank  	sisvbblank;
	sisfb_info        	x;
	u32			gpu32 = 0;
#ifndef __user
#define __user
#endif
	u32 __user 		*argp = (u32 __user *)arg;

	switch (cmd) {
	   case FBIO_ALLOC:
		if(!capable(CAP_SYS_RAWIO)) {
			return -EPERM;
		}
		if(copy_from_user(&sismemreq, (void __user *)arg, sizeof(sismemreq))) {
		   	return -EFAULT;
		}
		sis_malloc(&sismemreq);
		if(copy_to_user((void __user *)arg, &sismemreq, sizeof(sismemreq))) {
			sis_free((u32)sismemreq.offset);
		    	return -EFAULT;
		}
		break;

	   case FBIO_FREE:
		if(!capable(CAP_SYS_RAWIO)) {
			return -EPERM;
		}
		if(get_user(gpu32, argp)) {
			return -EFAULT;
		}
		sis_free(gpu32);
		break;

	   case FBIOGET_VBLANK:
		sisvbblank.count = 0;
		sisvbblank.flags = sisfb_setupvbblankflags(ivideo, &sisvbblank.vcount, &sisvbblank.hcount);
		if(copy_to_user((void __user *)arg, &sisvbblank, sizeof(sisvbblank))) {
			return -EFAULT;
		}
		break;

	   case SISFB_GET_INFO_SIZE:
	        return put_user(sizeof(sisfb_info), argp);

	   case SISFB_GET_INFO_OLD:
	        if(ivideo->warncount++ < 50) {
	           printk(KERN_INFO "sisfb: Deprecated ioctl call received - update your application!\n");
		}
	   case SISFB_GET_INFO:  /* For communication with X driver */
		x.sisfb_id         = SISFB_ID;
		x.sisfb_version    = VER_MAJOR;
		x.sisfb_revision   = VER_MINOR;
		x.sisfb_patchlevel = VER_LEVEL;
		x.chip_id = ivideo->chip_id;
		x.memory = ivideo->video_size / 1024;
		x.heapstart = ivideo->heapstart / 1024;
		if(ivideo->modechanged) {
		   x.fbvidmode = ivideo->mode_no;
		} else {
		   x.fbvidmode = ivideo->modeprechange;
		}
		x.sisfb_caps = ivideo->caps;
		x.sisfb_tqlen = 512; /* yet fixed */
		x.sisfb_pcibus = ivideo->pcibus;
		x.sisfb_pcislot = ivideo->pcislot;
		x.sisfb_pcifunc = ivideo->pcifunc;
		x.sisfb_lcdpdc = ivideo->detectedpdc;
		x.sisfb_lcdpdca = ivideo->detectedpdca;
		x.sisfb_lcda = ivideo->detectedlcda;
		x.sisfb_vbflags = ivideo->vbflags;
		x.sisfb_currentvbflags = ivideo->currentvbflags;
		x.sisfb_scalelcd = ivideo->SiS_Pr.UsePanelScaler;
		x.sisfb_specialtiming = ivideo->SiS_Pr.SiS_CustomT;
		x.sisfb_haveemi = ivideo->SiS_Pr.HaveEMI ? 1 : 0;
		x.sisfb_haveemilcd = ivideo->SiS_Pr.HaveEMILCD ? 1 : 0;
		x.sisfb_emi30 = ivideo->SiS_Pr.EMI_30;
		x.sisfb_emi31 = ivideo->SiS_Pr.EMI_31;
		x.sisfb_emi32 = ivideo->SiS_Pr.EMI_32;
		x.sisfb_emi33 = ivideo->SiS_Pr.EMI_33;
		x.sisfb_tvxpos = (u16)(ivideo->tvxpos + 32);
		x.sisfb_tvypos = (u16)(ivideo->tvypos + 32);

		if(copy_to_user((void __user *)arg, &x, sizeof(x))) {
			return -EFAULT;
		}
	        break;

	   case SISFB_GET_VBRSTATUS_OLD:
	   	if(ivideo->warncount++ < 50) {
	           printk(KERN_INFO "sisfb: Deprecated ioctl call received - update your application!\n");
		}
	   case SISFB_GET_VBRSTATUS:
	        if(sisfb_CheckVBRetrace(ivideo)) {
			return put_user((u32)1, argp);
		} else {
			return put_user((u32)0, argp);
		}

	   case SISFB_GET_AUTOMAXIMIZE_OLD:
	   	if(ivideo->warncount++ < 50) {
	           printk(KERN_INFO "sisfb: Deprecated ioctl call received - update your application!\n");
		}
	   case SISFB_GET_AUTOMAXIMIZE:
	        if(ivideo->sisfb_max)	return put_user((u32)1, argp);
		else			return put_user((u32)0, argp);

	   case SISFB_SET_AUTOMAXIMIZE_OLD:
	   	if(ivideo->warncount++ < 50) {
		   printk(KERN_INFO "sisfb: Deprecated ioctl call received - update your application!\n");
		}
	   case SISFB_SET_AUTOMAXIMIZE:
		if(copy_from_user(&gpu32, argp, sizeof(gpu32))) {
			return -EFAULT;
		}
		ivideo->sisfb_max = (gpu32) ? 1 : 0;
		break;

	   case SISFB_SET_TVPOSOFFSET:
		if(copy_from_user(&gpu32, argp, sizeof(gpu32))) {
			return -EFAULT;
		}
		sisfb_set_TVxposoffset(ivideo, ((int)(gpu32 >> 16)) - 32);
		sisfb_set_TVyposoffset(ivideo, ((int)(gpu32 & 0xffff)) - 32);
		break;

	   case SISFB_GET_TVPOSOFFSET:
	        return put_user((u32)(((ivideo->tvxpos+32)<<16)|((ivideo->tvypos+32)&0xffff)),
				argp);

	   case SISFB_SET_LOCK:
		if(copy_from_user(&gpu32, argp, sizeof(gpu32))) {
			return -EFAULT;
		}
		ivideo->sisfblocked = (gpu32) ? 1 : 0;
		break;

	   default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

#ifdef CONFIG_COMPAT
static long sisfb_compat_ioctl(struct file *f, unsigned cmd, unsigned long arg, struct fb_info *info)
{
	int ret;
	lock_kernel();
	ret = sisfb_ioctl(NULL, f, cmd, arg, info);
	unlock_kernel();
	return ret;
}
#endif

static int
sisfb_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

	strcpy(fix->id, ivideo->myid);

	fix->smem_start  = ivideo->video_base;
	fix->smem_len    = ivideo->sisfb_mem;
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
	   fix->accel    = FB_ACCEL_SIS_GLAMOUR;
	} else if((ivideo->chip == SIS_330) || (ivideo->chip == SIS_760)) {
	   fix->accel    = FB_ACCEL_SIS_XABRE;
	} else {
	   fix->accel    = FB_ACCEL_SIS_GLAMOUR_2;
	}

	return 0;
}

/* ----------------  fb_ops structures ----------------- */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static struct fb_ops sisfb_ops = {
	.owner		= THIS_MODULE,
	.fb_get_fix	= sisfb_get_fix,
	.fb_get_var	= sisfb_get_var,
	.fb_set_var	= sisfb_set_var,
	.fb_get_cmap	= sisfb_get_cmap,
	.fb_set_cmap	= sisfb_set_cmap,
        .fb_pan_display = sisfb_pan_display,
	.fb_ioctl	= sisfb_ioctl
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static struct fb_ops sisfb_ops = {
	.owner          = THIS_MODULE,
	.fb_open        = sisfb_open,
	.fb_release     = sisfb_release,
	.fb_check_var   = sisfb_check_var,
	.fb_set_par     = sisfb_set_par,
	.fb_setcolreg   = sisfb_setcolreg,
        .fb_pan_display = sisfb_pan_display,
        .fb_blank       = sisfb_blank,
	.fb_fillrect    = fbcon_sis_fillrect,
	.fb_copyarea    = fbcon_sis_copyarea,
	.fb_imageblit   = cfb_imageblit,
	.fb_cursor      = soft_cursor,
	.fb_sync        = fbcon_sis_sync,
	.fb_ioctl       = sisfb_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl = sisfb_compat_ioctl,
#endif
};
#endif

/* ---------------- Chip generation dependent routines ---------------- */

static struct pci_dev * sisfb_get_northbridge(int basechipid)
{
	struct pci_dev *pdev = NULL;
	int nbridgenum, nbridgeidx, i;
	const unsigned short nbridgeids[] = {
		PCI_DEVICE_ID_SI_540,	/* for SiS 540 VGA */
		PCI_DEVICE_ID_SI_630,	/* for SiS 630/730 VGA */
		PCI_DEVICE_ID_SI_730,
		PCI_DEVICE_ID_SI_550,   /* for SiS 550 VGA */
		PCI_DEVICE_ID_SI_650,   /* for SiS 650/651/740 VGA */
		PCI_DEVICE_ID_SI_651,
		PCI_DEVICE_ID_SI_740,
		PCI_DEVICE_ID_SI_661,	/* for SiS 661/741/660/760 VGA */
		PCI_DEVICE_ID_SI_741,
		PCI_DEVICE_ID_SI_660,
		PCI_DEVICE_ID_SI_760
	};

    	switch(basechipid) {
#ifdef CONFIG_FB_SIS_300
	case SIS_540:	nbridgeidx = 0; nbridgenum = 1; break;
	case SIS_630:	nbridgeidx = 1; nbridgenum = 2; break;
#endif
#ifdef CONFIG_FB_SIS_315
	case SIS_550:   nbridgeidx = 3; nbridgenum = 1; break;
	case SIS_650:	nbridgeidx = 4; nbridgenum = 3; break;
	case SIS_660:	nbridgeidx = 7; nbridgenum = 4; break;
#endif
	default:	return NULL;
	}
	for(i = 0; i < nbridgenum; i++) {
		if((pdev = pci_find_device(PCI_VENDOR_ID_SI, nbridgeids[nbridgeidx+i], NULL))) break;
	}
	return pdev;
}

static int __devinit sisfb_get_dram_size(struct sis_video_info *ivideo)
{
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
	u8 reg;
#endif

	ivideo->video_size = 0;

	switch(ivideo->chip) {
#ifdef CONFIG_FB_SIS_300
	case SIS_300:
	        inSISIDXREG(SISSR, 0x14, reg);
		ivideo->video_size = ((reg & 0x3F) + 1) << 20;
		break;
	case SIS_540:
	case SIS_630:
	case SIS_730:
	   	if(!ivideo->nbridge) return -1;
	   	pci_read_config_byte(ivideo->nbridge, 0x63, &reg);
		ivideo->video_size = 1 << (((reg & 0x70) >> 4) + 21);
		break;
#endif
#ifdef CONFIG_FB_SIS_315
	case SIS_315H:
	case SIS_315PRO:
	case SIS_315:
	   	inSISIDXREG(SISSR, 0x14, reg);
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
	   	inSISIDXREG(SISSR, 0x14, reg);
		ivideo->video_size = (1 << ((reg & 0xf0) >> 4)) << 20;
		if(reg & 0x0c) ivideo->video_size <<= 1;
	   	break;
	case SIS_550:
	case SIS_650:
	case SIS_740:
	    	inSISIDXREG(SISSR, 0x14, reg);
		ivideo->video_size = (((reg & 0x3f) + 1) << 2) << 20;
		break;
	case SIS_661:
	case SIS_741:
	     	inSISIDXREG(SISCR, 0x79, reg);
		ivideo->video_size = (1 << ((reg & 0xf0) >> 4)) << 20;
	   	break;
	case SIS_660:
	case SIS_760:
		inSISIDXREG(SISCR, 0x79, reg);
		reg = (reg & 0xf0) >> 4;
		if(reg)	ivideo->video_size = (1 << reg) << 20;
		inSISIDXREG(SISCR, 0x78, reg);
		reg &= 0x30;
		if(reg) {
		   if(reg == 0x10) ivideo->video_size += (32 << 20);
		   else		   ivideo->video_size += (64 << 20);
		}
	   	break;
#endif
	default:
		return -1;
	}
	return 0;
}

/* -------------- video bridge device detection --------------- */

static void __devinit sisfb_detect_VB_connect(struct sis_video_info *ivideo)
{
	u8 cr32, temp;

#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		inSISIDXREG(SISSR, 0x17, temp);
		if((temp & 0x0F) && (ivideo->chip != SIS_300)) {
			/* PAL/NTSC is stored on SR16 on such machines */
			if(!(ivideo->vbflags & (TV_PAL | TV_NTSC | TV_PALM | TV_PALN))) {
		   		inSISIDXREG(SISSR, 0x16, temp);
				if(temp & 0x20)
					ivideo->vbflags |= TV_PAL;
				else
					ivideo->vbflags |= TV_NTSC;
			}
		}
	}
#endif

	inSISIDXREG(SISCR, 0x32, cr32);

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
	       (!(ivideo->vbflags & (VB_301C|VB_301LV|VB_302LV))) ) {
	      if(ivideo->sisfb_tvplug & TV_YPBPR) {
	         ivideo->sisfb_tvplug = -1;
		 printk(KERN_ERR "sisfb: YPbPr not supported\n");
	      }
	   }
	}
	if(ivideo->sisfb_tvplug != -1) {
	   if( (ivideo->sisvga_engine != SIS_315_VGA) ||
	       (!(ivideo->vbflags & (VB_301|VB_301B|VB_302B))) ) {
	      if(ivideo->sisfb_tvplug & TV_HIVISION) {
	         ivideo->sisfb_tvplug = -1;
		 printk(KERN_ERR "sisfb: HiVision not supported\n");
	      }
	   }
	}
	if(ivideo->sisfb_tvstd != -1) {
	   if( (!(ivideo->vbflags & VB_SISBRIDGE)) &&
	       (!((ivideo->sisvga_engine == SIS_315_VGA) && (ivideo->vbflags & VB_CHRONTEL))) ) {
	      if(ivideo->sisfb_tvstd & (TV_PALN | TV_PALN | TV_NTSCJ)) {
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
	        	inSISIDXREG(SISSR, 0x38, temp);
			if(temp & 0x01) ivideo->vbflags |= TV_PAL;
			else		ivideo->vbflags |= TV_NTSC;
		} else if((ivideo->chip <= SIS_315PRO) || (ivideo->chip >= SIS_330)) {
                	inSISIDXREG(SISSR, 0x38, temp);
			if(temp & 0x01) ivideo->vbflags |= TV_PAL;
			else		ivideo->vbflags |= TV_NTSC;
	    	} else {
	        	inSISIDXREG(SISCR, 0x79, temp);
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

static void __devinit sisfb_get_VB_type(struct sis_video_info *ivideo)
{
	char stdstr[]    = "sisfb: Detected";
	char bridgestr[] = "video bridge";
	u8 vb_chipid;
	u8 reg;

	inSISIDXREG(SISPART4, 0x00, vb_chipid);
	switch(vb_chipid) {
	case 0x01:
		inSISIDXREG(SISPART4, 0x01, reg);
		if(reg < 0xb0) {
			ivideo->vbflags |= VB_301;
			printk(KERN_INFO "%s SiS301 %s\n", stdstr, bridgestr);
		} else if(reg < 0xc0) {
		 	ivideo->vbflags |= VB_301B;
			inSISIDXREG(SISPART4,0x23,reg);
			if(!(reg & 0x02)) {
			   ivideo->vbflags |= VB_30xBDH;
			   printk(KERN_INFO "%s SiS301B-DH %s\n", stdstr, bridgestr);
			} else {
			   printk(KERN_INFO "%s SiS301B %s\n", stdstr, bridgestr);
			}
		} else if(reg < 0xd0) {
		 	ivideo->vbflags |= VB_301C;
			printk(KERN_INFO "%s SiS301C %s\n", stdstr, bridgestr);
		} else if(reg < 0xe0) {
			ivideo->vbflags |= VB_301LV;
			printk(KERN_INFO "%s SiS301LV %s\n", stdstr, bridgestr);
		} else if(reg <= 0xe1) {
		        inSISIDXREG(SISPART4,0x39,reg);
			if(reg == 0xff) {
			   ivideo->vbflags |= VB_302LV;
			   printk(KERN_INFO "%s SiS302LV %s\n", stdstr, bridgestr);
			} else {
			   ivideo->vbflags |= VB_301C;
			   printk(KERN_INFO "%s SiS301C(P4) %s\n", stdstr, bridgestr);
#if 0
			   ivideo->vbflags |= VB_302ELV;
			   printk(KERN_INFO "%s SiS302ELV %s\n", stdstr, bridgestr);
#endif
			}
		}
		break;
	case 0x02:
		ivideo->vbflags |= VB_302B;
		printk(KERN_INFO "%s SiS302B %s\n", stdstr, bridgestr);
		break;
	}

	if((!(ivideo->vbflags & VB_VIDEOBRIDGE)) && (ivideo->chip != SIS_300)) {
		inSISIDXREG(SISCR, 0x37, reg);
		reg &= SIS_EXTERNAL_CHIP_MASK;
		reg >>= 1;
		if(ivideo->sisvga_engine == SIS_300_VGA) {
#ifdef CONFIG_FB_SIS_300
			switch(reg) {
			   case SIS_EXTERNAL_CHIP_LVDS:
				ivideo->vbflags |= VB_LVDS;
				break;
			   case SIS_EXTERNAL_CHIP_TRUMPION:
				ivideo->vbflags |= VB_TRUMPION;
				break;
			   case SIS_EXTERNAL_CHIP_CHRONTEL:
				ivideo->vbflags |= VB_CHRONTEL;
				break;
			   case SIS_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo->vbflags |= (VB_LVDS | VB_CHRONTEL);
				break;
			}
			if(ivideo->vbflags & VB_CHRONTEL) ivideo->chronteltype = 1;
#endif
		} else if(ivideo->chip < SIS_661) {
#ifdef CONFIG_FB_SIS_315
			switch (reg) {
	 	   	   case SIS310_EXTERNAL_CHIP_LVDS:
				ivideo->vbflags |= VB_LVDS;
				break;
		   	   case SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL:
				ivideo->vbflags |= (VB_LVDS | VB_CHRONTEL);
				break;
			}
			if(ivideo->vbflags & VB_CHRONTEL) ivideo->chronteltype = 2;
#endif
		} else if(ivideo->chip >= SIS_661) {
#ifdef CONFIG_FB_SIS_315
			inSISIDXREG(SISCR, 0x38, reg);
			reg >>= 5;
			switch(reg) {
			   case 0x02:
				ivideo->vbflags |= VB_LVDS;
				break;
			   case 0x03:
				ivideo->vbflags |= (VB_LVDS | VB_CHRONTEL);
				break;
			   case 0x04:
				ivideo->vbflags |= (VB_LVDS | VB_CONEXANT);
				break;
			}
			if(ivideo->vbflags & VB_CHRONTEL) ivideo->chronteltype = 2;
#endif
		}
		if(ivideo->vbflags & VB_LVDS) {
		   printk(KERN_INFO "%s LVDS transmitter\n", stdstr);
		}
		if(ivideo->vbflags & VB_TRUMPION) {
		   printk(KERN_INFO "%s Trumpion Zurac LCD scaler\n", stdstr);
		}
		if(ivideo->vbflags & VB_CHRONTEL) {
		   printk(KERN_INFO "%s Chrontel TV encoder\n", stdstr);
		}
		if(ivideo->vbflags & VB_CONEXANT) {
		   printk(KERN_INFO "%s Conexant external device\n", stdstr);
		}
	}

	if(ivideo->vbflags & VB_SISBRIDGE) {
		SiS_Sense30x(ivideo);
	} else if(ivideo->vbflags & VB_CHRONTEL) {
		SiS_SenseCh(ivideo);
	}
}

/* ------------------ Sensing routines ------------------ */

static BOOLEAN __devinit sisfb_test_DDC1(struct sis_video_info *ivideo)
{
    unsigned short old;
    int count = 48;

    old = SiS_ReadDDC1Bit(&ivideo->SiS_Pr);
    do {
       	if(old != SiS_ReadDDC1Bit(&ivideo->SiS_Pr)) break;
    } while(count--);
    return (count == -1) ? FALSE : TRUE;
}

static void __devinit sisfb_sense_crt1(struct sis_video_info *ivideo)
{
    BOOLEAN mustwait = FALSE;
    u8  sr1F, cr17;
#ifdef CONFIG_FB_SIS_315
    u8  cr63=0;
#endif
    u16 temp = 0xffff;
    int i;

    inSISIDXREG(SISSR,0x1F,sr1F);
    orSISIDXREG(SISSR,0x1F,0x04);
    andSISIDXREG(SISSR,0x1F,0x3F);
    if(sr1F & 0xc0) mustwait = TRUE;

#ifdef CONFIG_FB_SIS_315
    if(ivideo->sisvga_engine == SIS_315_VGA) {
       inSISIDXREG(SISCR,ivideo->SiS_Pr.SiS_MyCR63,cr63);
       cr63 &= 0x40;
       andSISIDXREG(SISCR,ivideo->SiS_Pr.SiS_MyCR63,0xBF);
    }
#endif

    inSISIDXREG(SISCR,0x17,cr17);
    cr17 &= 0x80;
    if(!cr17) {
       orSISIDXREG(SISCR,0x17,0x80);
       mustwait = TRUE;
       outSISIDXREG(SISSR, 0x00, 0x01);
       outSISIDXREG(SISSR, 0x00, 0x03);
    }

    if(mustwait) {
       for(i=0; i < 10; i++) sisfbwaitretracecrt1(ivideo);
    }

#ifdef CONFIG_FB_SIS_315
    if(ivideo->chip >= SIS_330) {
       andSISIDXREG(SISCR,0x32,~0x20);
       if(ivideo->chip >= SIS_340) {
          outSISIDXREG(SISCR, 0x57, 0x4a);
       } else {
          outSISIDXREG(SISCR, 0x57, 0x5f);
       }
       orSISIDXREG(SISCR, 0x53, 0x02);
       while((inSISREG(SISINPSTAT)) & 0x01)    break;
       while(!((inSISREG(SISINPSTAT)) & 0x01)) break;
       if((inSISREG(SISMISCW)) & 0x10) temp = 1;
       andSISIDXREG(SISCR, 0x53, 0xfd);
       andSISIDXREG(SISCR, 0x57, 0x00);
    }
#endif

    if(temp == 0xffff) {
       i = 3;
       do {
          temp = SiS_HandleDDC(&ivideo->SiS_Pr, ivideo->vbflags, ivideo->sisvga_engine, 0, 0, NULL);
       } while(((temp == 0) || (temp == 0xffff)) && i--);

       if((temp == 0) || (temp == 0xffff)) {
          if(sisfb_test_DDC1(ivideo)) temp = 1;
       }
    }

    if((temp) && (temp != 0xffff)) {
       orSISIDXREG(SISCR,0x32,0x20);
    }

#ifdef CONFIG_FB_SIS_315
    if(ivideo->sisvga_engine == SIS_315_VGA) {
       setSISIDXREG(SISCR,ivideo->SiS_Pr.SiS_MyCR63,0xBF,cr63);
    }
#endif

    setSISIDXREG(SISCR,0x17,0x7F,cr17);

    outSISIDXREG(SISSR,0x1F,sr1F);
}

/* Determine and detect attached devices on SiS30x */
static int __devinit SISDoSense(struct sis_video_info *ivideo, u16 type, u16 test)
{
    int temp, mytest, result, i, j;

    for(j = 0; j < 10; j++) {
       result = 0;
       for(i = 0; i < 3; i++) {
          mytest = test;
          outSISIDXREG(SISPART4,0x11,(type & 0x00ff));
          temp = (type >> 8) | (mytest & 0x00ff);
          setSISIDXREG(SISPART4,0x10,0xe0,temp);
          SiS_DDC2Delay(&ivideo->SiS_Pr, 0x1500);
          mytest >>= 8;
          mytest &= 0x7f;
          inSISIDXREG(SISPART4,0x03,temp);
          temp ^= 0x0e;
          temp &= mytest;
          if(temp == mytest) result++;
#if 1
	  outSISIDXREG(SISPART4,0x11,0x00);
	  andSISIDXREG(SISPART4,0x10,0xe0);
	  SiS_DDC2Delay(&ivideo->SiS_Pr, 0x1000);
#endif
       }
       if((result == 0) || (result >= 2)) break;
    }
    return(result);
}

static void __devinit SiS_Sense30x(struct sis_video_info *ivideo)
{
    u8  backupP4_0d,backupP2_00,backupP2_4d,backupSR_1e,biosflag=0;
    u16 svhs=0, svhs_c=0;
    u16 cvbs=0, cvbs_c=0;
    u16 vga2=0, vga2_c=0;
    int myflag, result;
    char stdstr[] = "sisfb: Detected";
    char tvstr[]  = "TV connected to";

    if(ivideo->vbflags & VB_301) {
       svhs = 0x00b9; cvbs = 0x00b3; vga2 = 0x00d1;
       inSISIDXREG(SISPART4,0x01,myflag);
       if(myflag & 0x04) {
	  svhs = 0x00dd; cvbs = 0x00ee; vga2 = 0x00fd;
       }
    } else if(ivideo->vbflags & (VB_301B | VB_302B)) {
       svhs = 0x016b; cvbs = 0x0174; vga2 = 0x0190;
    } else if(ivideo->vbflags & (VB_301LV | VB_302LV)) {
       svhs = 0x0200; cvbs = 0x0100;
    } else if(ivideo->vbflags & (VB_301C | VB_302ELV)) {
       svhs = 0x016b; cvbs = 0x0110; vga2 = 0x0190;
    } else return;

    vga2_c = 0x0e08; svhs_c = 0x0404; cvbs_c = 0x0804;
    if(ivideo->vbflags & (VB_301LV|VB_302LV|VB_302ELV)) {
       svhs_c = 0x0408; cvbs_c = 0x0808;
    }
    biosflag = 2;

    if(ivideo->chip == SIS_300) {
       inSISIDXREG(SISSR,0x3b,myflag);
       if(!(myflag & 0x01)) vga2 = vga2_c = 0;
    }

    inSISIDXREG(SISSR,0x1e,backupSR_1e);
    orSISIDXREG(SISSR,0x1e,0x20);

    inSISIDXREG(SISPART4,0x0d,backupP4_0d);
    if(ivideo->vbflags & VB_301C) {
       setSISIDXREG(SISPART4,0x0d,~0x07,0x01);
    } else {
       orSISIDXREG(SISPART4,0x0d,0x04);
    }
    SiS_DDC2Delay(&ivideo->SiS_Pr, 0x2000);

    inSISIDXREG(SISPART2,0x00,backupP2_00);
    outSISIDXREG(SISPART2,0x00,((backupP2_00 | 0x1c) & 0xfc));

    inSISIDXREG(SISPART2,0x4d,backupP2_4d);
    if(ivideo->vbflags & (VB_301C|VB_301LV|VB_302LV|VB_302ELV)) {
       outSISIDXREG(SISPART2,0x4d,(backupP2_4d & ~0x10));
    }

    if(!(ivideo->vbflags & VB_301C)) {
       SISDoSense(ivideo, 0, 0);
    }

    andSISIDXREG(SISCR, 0x32, ~0x14);

    if(vga2_c || vga2) {
       if(SISDoSense(ivideo, vga2, vga2_c)) {
          if(biosflag & 0x01) {
	     printk(KERN_INFO "%s %s SCART output\n", stdstr, tvstr);
	     orSISIDXREG(SISCR, 0x32, 0x04);
	  } else {
	     printk(KERN_INFO "%s secondary VGA connection\n", stdstr);
	     orSISIDXREG(SISCR, 0x32, 0x10);
	  }
       }
    }

    andSISIDXREG(SISCR, 0x32, 0x3f);

    if(ivideo->vbflags & VB_301C) {
       orSISIDXREG(SISPART4,0x0d,0x04);
    }

    if((ivideo->sisvga_engine == SIS_315_VGA) &&
       (ivideo->vbflags & (VB_301C|VB_301LV|VB_302LV|VB_302ELV))) {
       outSISIDXREG(SISPART2,0x4d,(backupP2_4d | 0x10));
       SiS_DDC2Delay(&ivideo->SiS_Pr, 0x2000);
       if((result = SISDoSense(ivideo, svhs, 0x0604))) {
          if((result = SISDoSense(ivideo, cvbs, 0x0804))) {
	     printk(KERN_INFO "%s %s YPbPr component output\n", stdstr, tvstr);
	     orSISIDXREG(SISCR,0x32,0x80);
	  }
       }
       outSISIDXREG(SISPART2,0x4d,backupP2_4d);
    }

    andSISIDXREG(SISCR, 0x32, ~0x03);

    if(!(ivideo->vbflags & TV_YPBPR)) {
       if((result = SISDoSense(ivideo, svhs, svhs_c))) {
          printk(KERN_INFO "%s %s SVIDEO output\n", stdstr, tvstr);
          orSISIDXREG(SISCR, 0x32, 0x02);
       }
       if((biosflag & 0x02) || (!result)) {
          if(SISDoSense(ivideo, cvbs, cvbs_c)) {
	     printk(KERN_INFO "%s %s COMPOSITE output\n", stdstr, tvstr);
	     orSISIDXREG(SISCR, 0x32, 0x01);
          }
       }
    }

    SISDoSense(ivideo, 0, 0);

    outSISIDXREG(SISPART2,0x00,backupP2_00);
    outSISIDXREG(SISPART4,0x0d,backupP4_0d);
    outSISIDXREG(SISSR,0x1e,backupSR_1e);

    if(ivideo->vbflags & VB_301C) {
       inSISIDXREG(SISPART2,0x00,biosflag);
       if(biosflag & 0x20) {
          for(myflag = 2; myflag > 0; myflag--) {
	     biosflag ^= 0x20;
	     outSISIDXREG(SISPART2,0x00,biosflag);
	  }
       }
    }

    outSISIDXREG(SISPART2,0x00,backupP2_00);
}

/* Determine and detect attached TV's on Chrontel */
static void __devinit SiS_SenseCh(struct sis_video_info *ivideo)
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
	  SiS_SetCH700x(&ivideo->SiS_Pr, 0x0b0e);
	  SiS_DDC2Delay(&ivideo->SiS_Pr, 300);
       }
       temp2 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x25);
       if(temp2 != temp1) temp1 = temp2;

       if((temp1 >= 0x22) && (temp1 <= 0x50)) {
	   /* Read power status */
	   temp1 = SiS_GetCH700x(&ivideo->SiS_Pr, 0x0e);
	   if((temp1 & 0x03) != 0x03) {
     	        /* Power all outputs */
		SiS_SetCH700x(&ivideo->SiS_Pr, 0x0B0E);
		SiS_DDC2Delay(&ivideo->SiS_Pr, 300);
	   }
	   /* Sense connected TV devices */
	   for(i = 0; i < 3; i++) {
	       SiS_SetCH700x(&ivideo->SiS_Pr, 0x0110);
	       SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	       SiS_SetCH700x(&ivideo->SiS_Pr, 0x0010);
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
		orSISIDXREG(SISCR, 0x32, 0x02);
		andSISIDXREG(SISCR, 0x32, ~0x05);
	   } else if (temp1 == 0x01) {
		printk(KERN_INFO "%s CVBS output\n", stdstr);
		ivideo->vbflags |= TV_AVIDEO;
		orSISIDXREG(SISCR, 0x32, 0x01);
		andSISIDXREG(SISCR, 0x32, ~0x06);
	   } else {
 		SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, 0x010E,0xF8);
		andSISIDXREG(SISCR, 0x32, ~0x07);
	   }
       } else if(temp1 == 0) {
	  SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, 0x010E,0xF8);
	  andSISIDXREG(SISCR, 0x32, ~0x07);
       }
       /* Set general purpose IO for Chrontel communication */
       SiS_SetChrontelGPIO(&ivideo->SiS_Pr, 0x00);
#endif

    } else {

#ifdef CONFIG_FB_SIS_315
	ivideo->SiS_Pr.SiS_IF_DEF_CH70xx = 2;		/* Chrontel 7019 */
        temp1 = SiS_GetCH701x(&ivideo->SiS_Pr, 0x49);
	SiS_SetCH701x(&ivideo->SiS_Pr, 0x2049);
	SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	temp2 = SiS_GetCH701x(&ivideo->SiS_Pr, 0x20);
	temp2 |= 0x01;
	SiS_SetCH701x(&ivideo->SiS_Pr, (temp2 << 8) | 0x20);
	SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	temp2 ^= 0x01;
	SiS_SetCH701x(&ivideo->SiS_Pr, (temp2 << 8) | 0x20);
	SiS_DDC2Delay(&ivideo->SiS_Pr, 0x96);
	temp2 = SiS_GetCH701x(&ivideo->SiS_Pr, 0x20);
	SiS_SetCH701x(&ivideo->SiS_Pr, (temp1 << 8) | 0x49);
        temp1 = 0;
	if(temp2 & 0x02) temp1 |= 0x01;
	if(temp2 & 0x10) temp1 |= 0x01;
	if(temp2 & 0x04) temp1 |= 0x02;
	if( (temp1 & 0x01) && (temp1 & 0x02) ) temp1 = 0x04;
	switch(temp1) {
	case 0x01:
	     printk(KERN_INFO "%s CVBS output\n", stdstr);
	     ivideo->vbflags |= TV_AVIDEO;
	     orSISIDXREG(SISCR, 0x32, 0x01);
	     andSISIDXREG(SISCR, 0x32, ~0x06);
             break;
	case 0x02:
	     printk(KERN_INFO "%s SVIDEO output\n", stdstr);
	     ivideo->vbflags |= TV_SVIDEO;
	     orSISIDXREG(SISCR, 0x32, 0x02);
	     andSISIDXREG(SISCR, 0x32, ~0x05);
             break;
	case 0x04:
	     printk(KERN_INFO "%s SCART output\n", stdstr);
	     orSISIDXREG(SISCR, 0x32, 0x04);
	     andSISIDXREG(SISCR, 0x32, ~0x03);
             break;
	default:
	     andSISIDXREG(SISCR, 0x32, ~0x07);
	}
#endif
    }
}

/* ------------------------ Heap routines -------------------------- */

static u32 __devinit
sisfb_getheapstart(struct sis_video_info *ivideo)
{
	u32 ret = ivideo->sisfb_parm_mem * 1024;
	u32 max = ivideo->video_size - ivideo->hwcursor_size;
	u32 def;

	/* Calculate heap start = end of memory for console
	 *
	 * CCCCCCCCDDDDDDDDDDDDDDDDDDDDDDDDDDDDHHHHQQQQQQQQQQ
	 * C = console, D = heap, H = HWCursor, Q = cmd-queue
	 *
	 * Basically given by "mem" parameter
	 *
	 * maximum = videosize - cmd_queue - hwcursor
	 *           (results in a heap of size 0)
	 * default = SiS 300: depends on videosize
	 *           SiS 315/330: 32k below max
	 */

	if(ivideo->sisvga_engine == SIS_300_VGA) {
	   max -= TURBO_QUEUE_AREA_SIZE;
	   if(ivideo->video_size > 0x1000000) {
	      def = 0xc00000;
	   } else if(ivideo->video_size > 0x800000) {
	      def = 0x800000;
	   } else {
	      def = 0x400000;
	   }
	} else {
	   max -= COMMAND_QUEUE_AREA_SIZE;
	   def = max - 0x8000;
	}

        if((!ret) || (ret > max) || (ivideo->cardnumber != 0)) {
	   ret = def;
        }

	return ret;
}

static int __devinit
sisfb_heap_init(struct sis_video_info *ivideo)
{
     SIS_OH *poh;

     ivideo->heapstart = ivideo->sisfb_mem = sisfb_getheapstart(ivideo);

     ivideo->sisfb_heap_start = ivideo->video_vbase + ivideo->heapstart;
     ivideo->sisfb_heap_end   = ivideo->video_vbase + ivideo->video_size;

     /* Initialize command queue (We use MMIO only) */

#ifdef CONFIG_FB_SIS_315
     if(ivideo->sisvga_engine == SIS_315_VGA) {
        u32 tempq = 0;
	u8  temp = 0;

        ivideo->sisfb_heap_end -= COMMAND_QUEUE_AREA_SIZE;

	outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_THRESHOLD, COMMAND_QUEUE_THRESHOLD);
	outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, SIS_CMD_QUEUE_RESET);

	tempq = MMIO_IN32(ivideo->mmio_vbase, MMIO_QUEUE_READPORT);
	MMIO_OUT32(ivideo->mmio_vbase, MMIO_QUEUE_WRITEPORT, tempq);

	temp = SIS_CMD_QUEUE_SIZE_512k;
	temp |= (SIS_MMIO_CMD_ENABLE | SIS_CMD_AUTO_CORR);
	outSISIDXREG(SISSR, IND_SIS_CMDQUEUE_SET, temp);

	tempq = (u32)(ivideo->video_size - COMMAND_QUEUE_AREA_SIZE);
	MMIO_OUT32(ivideo->mmio_vbase, MMIO_QUEUE_PHYBASE, tempq);

	ivideo->caps |= MMIO_CMD_QUEUE_CAP;
     }
#endif

#ifdef CONFIG_FB_SIS_300
     if(ivideo->sisvga_engine == SIS_300_VGA) {
     	unsigned long tqueue_pos;
	u8 tq_state;

	ivideo->sisfb_heap_end -= TURBO_QUEUE_AREA_SIZE;

	tqueue_pos = (ivideo->video_size - TURBO_QUEUE_AREA_SIZE) / (64 * 1024);

	inSISIDXREG(SISSR, IND_SIS_TURBOQUEUE_SET, tq_state);
	tq_state |= 0xf0;
	tq_state &= 0xfc;
	tq_state |= (u8)(tqueue_pos >> 8);
	outSISIDXREG(SISSR, IND_SIS_TURBOQUEUE_SET, tq_state);

	outSISIDXREG(SISSR, IND_SIS_TURBOQUEUE_ADR, (u8)(tqueue_pos & 0xff));

	ivideo->caps |= TURBO_QUEUE_CAP;
     }
#endif

     /* Reserve memory for the HWCursor */
     ivideo->sisfb_heap_end -= ivideo->hwcursor_size;
     ivideo->hwcursor_vbase = ivideo->sisfb_heap_end;
     ivideo->caps |= HW_CURSOR_CAP;

     ivideo->sisfb_heap_size = ivideo->sisfb_heap_end - ivideo->sisfb_heap_start;

     if(ivideo->cardnumber == 0) {

     	printk(KERN_INFO "sisfb: Memory heap starting at %dK, size %dK\n",
     		(int)(ivideo->heapstart / 1024), (int)(ivideo->sisfb_heap_size / 1024));

	sisfb_heap.vinfo = ivideo;

     	sisfb_heap.poha_chain = NULL;
     	sisfb_heap.poh_freelist = NULL;

     	poh = sisfb_poh_new_node();
     	if(poh == NULL) return 1;

     	poh->poh_next = &sisfb_heap.oh_free;
     	poh->poh_prev = &sisfb_heap.oh_free;
     	poh->size = ivideo->sisfb_heap_size;
     	poh->offset = ivideo->heapstart;

     	sisfb_heap.oh_free.poh_next = poh;
     	sisfb_heap.oh_free.poh_prev = poh;
     	sisfb_heap.oh_free.size = 0;
     	sisfb_heap.max_freesize = poh->size;

     	sisfb_heap.oh_used.poh_next = &sisfb_heap.oh_used;
     	sisfb_heap.oh_used.poh_prev = &sisfb_heap.oh_used;
     	sisfb_heap.oh_used.size = SENTINEL;

     } else {

        printk(KERN_INFO "Skipped heap initialization for secondary cards\n");

     }

     return 0;
}

static SIS_OH *
sisfb_poh_new_node(void)
{
	int           i;
	unsigned long cOhs;
	SIS_OHALLOC   *poha;
	SIS_OH        *poh;

	if(sisfb_heap.poh_freelist == NULL) {
		poha = kmalloc(SIS_OH_ALLOC_SIZE, GFP_KERNEL);
		if(!poha) return NULL;

		poha->poha_next = sisfb_heap.poha_chain;
		sisfb_heap.poha_chain = poha;

		cOhs = (SIS_OH_ALLOC_SIZE - sizeof(SIS_OHALLOC)) / sizeof(SIS_OH) + 1;

		poh = &poha->aoh[0];
		for(i = cOhs - 1; i != 0; i--) {
			poh->poh_next = poh + 1;
			poh = poh + 1;
		}

		poh->poh_next = NULL;
		sisfb_heap.poh_freelist = &poha->aoh[0];
	}

	poh = sisfb_heap.poh_freelist;
	sisfb_heap.poh_freelist = poh->poh_next;

	return (poh);
}

static SIS_OH *
sisfb_poh_allocate(u32 size)
{
	SIS_OH *pohThis;
	SIS_OH *pohRoot;
	int     bAllocated = 0;

	if(size > sisfb_heap.max_freesize) {
		DPRINTK("sisfb: Can't allocate %dk video memory\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	pohThis = sisfb_heap.oh_free.poh_next;

	while(pohThis != &sisfb_heap.oh_free) {
		if (size <= pohThis->size) {
			bAllocated = 1;
			break;
		}
		pohThis = pohThis->poh_next;
	}

	if(!bAllocated) {
		DPRINTK("sisfb: Can't allocate %dk video memory\n",
			(unsigned int) size / 1024);
		return (NULL);
	}

	if(size == pohThis->size) {
		pohRoot = pohThis;
		sisfb_delete_node(pohThis);
	} else {
		pohRoot = sisfb_poh_new_node();

		if(pohRoot == NULL) {
			return (NULL);
		}

		pohRoot->offset = pohThis->offset;
		pohRoot->size = size;

		pohThis->offset += size;
		pohThis->size -= size;
	}

	sisfb_heap.max_freesize -= size;

	pohThis = &sisfb_heap.oh_used;
	sisfb_insert_node(pohThis, pohRoot);

	return (pohRoot);
}

static void
sisfb_delete_node(SIS_OH *poh)
{
	SIS_OH *poh_prev;
	SIS_OH *poh_next;

	poh_prev = poh->poh_prev;
	poh_next = poh->poh_next;

	poh_prev->poh_next = poh_next;
	poh_next->poh_prev = poh_prev;
}

static void
sisfb_insert_node(SIS_OH *pohList, SIS_OH *poh)
{
	SIS_OH *pohTemp;

	pohTemp = pohList->poh_next;

	pohList->poh_next = poh;
	pohTemp->poh_prev = poh;

	poh->poh_prev = pohList;
	poh->poh_next = pohTemp;
}

static SIS_OH *
sisfb_poh_free(u32 base)
{
	SIS_OH *pohThis;
	SIS_OH *poh_freed;
	SIS_OH *poh_prev;
	SIS_OH *poh_next;
	u32     ulUpper;
	u32     ulLower;
	int     foundNode = 0;

	poh_freed = sisfb_heap.oh_used.poh_next;

	while(poh_freed != &sisfb_heap.oh_used) {
		if(poh_freed->offset == base) {
			foundNode = 1;
			break;
		}

		poh_freed = poh_freed->poh_next;
	}

	if(!foundNode) return(NULL);

	sisfb_heap.max_freesize += poh_freed->size;

	poh_prev = poh_next = NULL;
	ulUpper = poh_freed->offset + poh_freed->size;
	ulLower = poh_freed->offset;

	pohThis = sisfb_heap.oh_free.poh_next;

	while(pohThis != &sisfb_heap.oh_free) {
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
		sisfb_free_node(poh_freed);
		sisfb_free_node(poh_next);
		return(poh_prev);
	}

	if(poh_prev) {
		poh_prev->size += poh_freed->size;
		sisfb_free_node(poh_freed);
		return(poh_prev);
	}

	if(poh_next) {
		poh_next->size += poh_freed->size;
		poh_next->offset = poh_freed->offset;
		sisfb_free_node(poh_freed);
		return(poh_next);
	}

	sisfb_insert_node(&sisfb_heap.oh_free, poh_freed);

	return(poh_freed);
}

static void
sisfb_free_node(SIS_OH *poh)
{
	if(poh == NULL) return;

	poh->poh_next = sisfb_heap.poh_freelist;
	sisfb_heap.poh_freelist = poh;
}

void
sis_malloc(struct sis_memreq *req)
{
	struct sis_video_info *ivideo = sisfb_heap.vinfo;
	SIS_OH *poh = NULL;

	if((ivideo) && (!ivideo->havenoheap)) {
	   poh = sisfb_poh_allocate((u32)req->size);
	}

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

/* sis_free: u32 because "base" is offset inside video ram, can never be >4GB */

void
sis_free(u32 base)
{
	struct sis_video_info *ivideo = sisfb_heap.vinfo;
	SIS_OH *poh;

	if((!ivideo) || (ivideo->havenoheap)) return;

	poh = sisfb_poh_free((u32)base);

	if(poh == NULL) {
		DPRINTK("sisfb: sisfb_poh_free() failed at base 0x%x\n",
			(unsigned int) base);
	}
}

/* --------------------- SetMode routines ------------------------- */

static void
sisfb_pre_setmode(struct sis_video_info *ivideo)
{
	u8 cr30 = 0, cr31 = 0, cr33 = 0, cr35 = 0, cr38 = 0;
	int tvregnum = 0;

	ivideo->currentvbflags &= (VB_VIDEOBRIDGE | VB_DISPTYPE_DISP2);

	inSISIDXREG(SISCR, 0x31, cr31);
	cr31 &= ~0x60;
	cr31 |= 0x04;

	cr33 = ivideo->rate_idx & 0x0F;

#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
	   if(ivideo->chip >= SIS_661) {
	      inSISIDXREG(SISCR, 0x38, cr38);
	      cr38 &= ~0x07;  /* Clear LCDA/DualEdge and YPbPr bits */
	   } else {
	      tvregnum = 0x38;
	      inSISIDXREG(SISCR, tvregnum, cr38);
	      cr38 &= ~0x3b;  /* Clear LCDA/DualEdge and YPbPr bits */
	   }
	}
#endif
#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
	   tvregnum = 0x35;
	   inSISIDXREG(SISCR, tvregnum, cr38);
	}
#endif

	SiS_SetEnableDstn(&ivideo->SiS_Pr, FALSE);
	SiS_SetEnableFstn(&ivideo->SiS_Pr, FALSE);

	switch(ivideo->currentvbflags & VB_DISPTYPE_DISP2) {

	   case CRT2_TV:
	      cr38 &= ~0xc0;   /* Clear PAL-M / PAL-N bits */
	      if((ivideo->vbflags & TV_YPBPR) && (ivideo->vbflags & (VB_301C|VB_301LV|VB_302LV))) {
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
	      } else if((ivideo->vbflags & TV_HIVISION) && (ivideo->vbflags & (VB_301|VB_301B|VB_302B))) {
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

	      if(ivideo->vbflags & (TV_AVIDEO|TV_SVIDEO)) {
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

	outSISIDXREG(SISCR, 0x30, cr30);
	outSISIDXREG(SISCR, 0x33, cr33);

	if(ivideo->chip >= SIS_661) {
#ifdef CONFIG_FB_SIS_315
	   cr31 &= ~0x01;                          /* Clear PAL flag (now in CR35) */
	   setSISIDXREG(SISCR, 0x35, ~0x10, cr35); /* Leave overscan bit alone */
	   cr38 &= 0x07;                           /* Use only LCDA and HiVision/YPbPr bits */
	   setSISIDXREG(SISCR, 0x38, 0xf8, cr38);
#endif
	} else if(ivideo->chip != SIS_300) {
	   outSISIDXREG(SISCR, tvregnum, cr38);
	}
	outSISIDXREG(SISCR, 0x31, cr31);

	if(ivideo->accel) sisfb_syncaccel(ivideo);

	ivideo->SiS_Pr.SiS_UseOEM = ivideo->sisfb_useoem;
}

/* Fix SR11 for 661 and later */
#ifdef CONFIG_FB_SIS_315
static void
sisfb_fixup_SR11(struct sis_video_info *ivideo)
{
    u8  tmpreg;

    if(ivideo->chip >= SIS_661) {
       inSISIDXREG(SISSR,0x11,tmpreg);
       if(tmpreg & 0x20) {
          inSISIDXREG(SISSR,0x3e,tmpreg);
	  tmpreg = (tmpreg + 1) & 0xff;
	  outSISIDXREG(SISSR,0x3e,tmpreg);
	  inSISIDXREG(SISSR,0x11,tmpreg);
       }
       if(tmpreg & 0xf0) {
          andSISIDXREG(SISSR,0x11,0x0f);
       }
    }
}
#endif

static void sisfb_set_TVxposoffset(struct sis_video_info *ivideo, int val)
{
   if(val > 32) val = 32;
   if(val < -32) val = -32;
   ivideo->tvxpos = val;

   if(ivideo->sisfblocked) return;
   if(!ivideo->modechanged) return;

   if(ivideo->currentvbflags & CRT2_TV) {

      if(ivideo->vbflags & VB_CHRONTEL) {

	 int x = ivideo->tvx;

	 switch(ivideo->chronteltype) {
	 case 1:
	     x += val;
	     if(x < 0) x = 0;
	     outSISIDXREG(SISSR,0x05,0x86);
	     SiS_SetCH700x(&ivideo->SiS_Pr, (((x & 0xff) << 8) | 0x0a));
	     SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, (((x & 0x0100) << 1) | 0x08),0xFD);
	     break;
	 case 2:
	     /* Not supported by hardware */
	     break;
	 }

      } else if(ivideo->vbflags & VB_SISBRIDGE) {

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
	 outSISIDXREG(SISPART2,0x1f,p2_1f);
	 setSISIDXREG(SISPART2,0x20,0x0F,p2_20);
	 setSISIDXREG(SISPART2,0x2b,0xF0,p2_2b);
	 setSISIDXREG(SISPART2,0x42,0x0F,p2_42);
	 outSISIDXREG(SISPART2,0x43,p2_43);
      }
   }
}

static void sisfb_set_TVyposoffset(struct sis_video_info *ivideo, int val)
{
   if(val > 32) val = 32;
   if(val < -32) val = -32;
   ivideo->tvypos = val;

   if(ivideo->sisfblocked) return;
   if(!ivideo->modechanged) return;

   if(ivideo->currentvbflags & CRT2_TV) {

      if(ivideo->vbflags & VB_CHRONTEL) {

	 int y = ivideo->tvy;

	 switch(ivideo->chronteltype) {
	 case 1:
	    y -= val;
	    if(y < 0) y = 0;
	    outSISIDXREG(SISSR,0x05,0x86);
	    SiS_SetCH700x(&ivideo->SiS_Pr, (((y & 0xff) << 8) | 0x0b));
	    SiS_SetCH70xxANDOR(&ivideo->SiS_Pr, ((y & 0x0100) | 0x08),0xFE);
	    break;
	 case 2:
	    /* Not supported by hardware */
	    break;
	 }

      } else if(ivideo->vbflags & VB_SISBRIDGE) {

	 char p2_01, p2_02;
	 val /= 2;
	 p2_01 = ivideo->p2_01;
	 p2_02 = ivideo->p2_02;

	 p2_01 += val;
	 p2_02 += val;
	 while((p2_01 <= 0) || (p2_02 <= 0)) {
	    p2_01 += 2;
	    p2_02 += 2;
	 }
	 outSISIDXREG(SISPART2,0x01,p2_01);
	 outSISIDXREG(SISPART2,0x02,p2_02);
      }
   }
}

static void
sisfb_post_setmode(struct sis_video_info *ivideo)
{
	BOOLEAN crt1isoff = FALSE;
	BOOLEAN doit = TRUE;
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
	u8 reg;
#endif
#ifdef CONFIG_FB_SIS_315
	u8 reg1;
#endif

	outSISIDXREG(SISSR,0x05,0x86);

#ifdef CONFIG_FB_SIS_315
	sisfb_fixup_SR11(ivideo);
#endif

	/* Now we actually HAVE changed the display mode */
        ivideo->modechanged = 1;

	/* We can't switch off CRT1 if bridge is in slave mode */
	if(ivideo->vbflags & VB_VIDEOBRIDGE) {
		if(sisfb_bridgeisslave(ivideo)) doit = FALSE;
	} else ivideo->sisfb_crt1off = 0;

#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
	   if((ivideo->sisfb_crt1off) && (doit)) {
	        crt1isoff = TRUE;
		reg = 0x00;
	   } else {
	        crt1isoff = FALSE;
		reg = 0x80;
	   }
	   setSISIDXREG(SISCR, 0x17, 0x7f, reg);
	}
#endif
#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
	   if((ivideo->sisfb_crt1off) && (doit)) {
	        crt1isoff = TRUE;
		reg  = 0x40;
		reg1 = 0xc0;
	   } else {
	        crt1isoff = FALSE;
		reg  = 0x00;
		reg1 = 0x00;

	   }
	   setSISIDXREG(SISCR, ivideo->SiS_Pr.SiS_MyCR63, ~0x40, reg);
	   setSISIDXREG(SISSR, 0x1f, ~0xc0, reg1);
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

        andSISIDXREG(SISSR, IND_SIS_RAMDAC_CONTROL, ~0x04);

	if(ivideo->currentvbflags & CRT2_TV) {
	   if(ivideo->vbflags & VB_SISBRIDGE) {
	      inSISIDXREG(SISPART2,0x1f,ivideo->p2_1f);
	      inSISIDXREG(SISPART2,0x20,ivideo->p2_20);
	      inSISIDXREG(SISPART2,0x2b,ivideo->p2_2b);
	      inSISIDXREG(SISPART2,0x42,ivideo->p2_42);
	      inSISIDXREG(SISPART2,0x43,ivideo->p2_43);
	      inSISIDXREG(SISPART2,0x01,ivideo->p2_01);
	      inSISIDXREG(SISPART2,0x02,ivideo->p2_02);
	   } else if(ivideo->vbflags & VB_CHRONTEL) {
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

	if((ivideo->currentvbflags & CRT2_TV) && (ivideo->vbflags & VB_301)) {  /* Set filter for SiS301 */

	   	unsigned char filter_tb = 0;

		switch (ivideo->video_width) {
		   case 320:
			filter_tb = (ivideo->vbflags & TV_NTSC) ? 4 : 12;
			break;
		   case 640:
			filter_tb = (ivideo->vbflags & TV_NTSC) ? 5 : 13;
			break;
		   case 720:
			filter_tb = (ivideo->vbflags & TV_NTSC) ? 6 : 14;
			break;
		   case 400:
		   case 800:
			filter_tb = (ivideo->vbflags & TV_NTSC) ? 7 : 15;
			break;
		   default:
			ivideo->sisfb_filter = -1;
			break;
		}

		orSISIDXREG(SISPART1, ivideo->CRT2_write_enable, 0x01);

		if(ivideo->vbflags & TV_NTSC) {

		        andSISIDXREG(SISPART2, 0x3a, 0x1f);

			if (ivideo->vbflags & TV_SVIDEO) {

			        andSISIDXREG(SISPART2, 0x30, 0xdf);

			} else if (ivideo->vbflags & TV_AVIDEO) {

			        orSISIDXREG(SISPART2, 0x30, 0x20);

				switch (ivideo->video_width) {
				case 640:
				        outSISIDXREG(SISPART2, 0x35, 0xEB);
					outSISIDXREG(SISPART2, 0x36, 0x04);
					outSISIDXREG(SISPART2, 0x37, 0x25);
					outSISIDXREG(SISPART2, 0x38, 0x18);
					break;
				case 720:
					outSISIDXREG(SISPART2, 0x35, 0xEE);
					outSISIDXREG(SISPART2, 0x36, 0x0C);
					outSISIDXREG(SISPART2, 0x37, 0x22);
					outSISIDXREG(SISPART2, 0x38, 0x08);
					break;
				case 400:
				case 800:
					outSISIDXREG(SISPART2, 0x35, 0xEB);
					outSISIDXREG(SISPART2, 0x36, 0x15);
					outSISIDXREG(SISPART2, 0x37, 0x25);
					outSISIDXREG(SISPART2, 0x38, 0xF6);
					break;
				}
			}

		} else if(ivideo->vbflags & TV_PAL) {

			andSISIDXREG(SISPART2, 0x3A, 0x1F);

			if (ivideo->vbflags & TV_SVIDEO) {

			        andSISIDXREG(SISPART2, 0x30, 0xDF);

			} else if (ivideo->vbflags & TV_AVIDEO) {

			        orSISIDXREG(SISPART2, 0x30, 0x20);

				switch (ivideo->video_width) {
				case 640:
					outSISIDXREG(SISPART2, 0x35, 0xF1);
					outSISIDXREG(SISPART2, 0x36, 0xF7);
					outSISIDXREG(SISPART2, 0x37, 0x1F);
					outSISIDXREG(SISPART2, 0x38, 0x32);
					break;
				case 720:
					outSISIDXREG(SISPART2, 0x35, 0xF3);
					outSISIDXREG(SISPART2, 0x36, 0x00);
					outSISIDXREG(SISPART2, 0x37, 0x1D);
					outSISIDXREG(SISPART2, 0x38, 0x20);
					break;
				case 400:
				case 800:
					outSISIDXREG(SISPART2, 0x35, 0xFC);
					outSISIDXREG(SISPART2, 0x36, 0xFB);
					outSISIDXREG(SISPART2, 0x37, 0x14);
					outSISIDXREG(SISPART2, 0x38, 0x2A);
					break;
				}
			}
		}

		if((ivideo->sisfb_filter >= 0) && (ivideo->sisfb_filter <= 7)) {
		   outSISIDXREG(SISPART2,0x35,(sis_TV_filter[filter_tb].filter[ivideo->sisfb_filter][0]));
		   outSISIDXREG(SISPART2,0x36,(sis_TV_filter[filter_tb].filter[ivideo->sisfb_filter][1]));
		   outSISIDXREG(SISPART2,0x37,(sis_TV_filter[filter_tb].filter[ivideo->sisfb_filter][2]));
		   outSISIDXREG(SISPART2,0x38,(sis_TV_filter[filter_tb].filter[ivideo->sisfb_filter][3]));
		}
	  
	}
}

#ifndef MODULE
SISINITSTATIC int __init sisfb_setup(char *options)
{
	char *this_opt;
	
	sisfb_setdefaultparms();

        printk(KERN_DEBUG "sisfb: Options %s\n", options);

	if(!options || !(*options)) {
		return 0;
	}

	while((this_opt = strsep(&options, ",")) != NULL) {

		if(!(*this_opt)) continue;

		if(!strnicmp(this_opt, "off", 3)) {
			sisfb_off = 1;
		} else if(!strnicmp(this_opt, "forcecrt2type:", 14)) {
			/* Need to check crt2 type first for fstn/dstn */
			sisfb_search_crt2type(this_opt + 14);
		} else if(!strnicmp(this_opt, "tvmode:",7)) {
		        sisfb_search_tvstd(this_opt + 7);
                } else if(!strnicmp(this_opt, "tvstandard:",11)) {
			sisfb_search_tvstd(this_opt + 7);
		} else if(!strnicmp(this_opt, "mode:", 5)) {
			sisfb_search_mode(this_opt + 5, FALSE);
		} else if(!strnicmp(this_opt, "vesa:", 5)) {
			sisfb_search_vesamode(simple_strtoul(this_opt + 5, NULL, 0), FALSE);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
		} else if(!strnicmp(this_opt, "inverse", 7)) {
			sisfb_inverse = 1;
			/* fb_invert_cmaps(); */
		} else if(!strnicmp(this_opt, "font:", 5)) {
		        if(strlen(this_opt + 5) < 40) {
			   strncpy(sisfb_fontname, this_opt + 5, sizeof(sisfb_fontname) - 1);
			   sisfb_fontname[sizeof(sisfb_fontname) - 1] = '\0';
			}
#endif
		} else if(!strnicmp(this_opt, "rate:", 5)) {
			sisfb_parm_rate = simple_strtoul(this_opt + 5, NULL, 0);
		} else if(!strnicmp(this_opt, "filter:", 7)) {
			sisfb_filter = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if(!strnicmp(this_opt, "forcecrt1:", 10)) {
			sisfb_forcecrt1 = (int)simple_strtoul(this_opt + 10, NULL, 0);
                } else if(!strnicmp(this_opt, "mem:",4)) {
		        sisfb_parm_mem = simple_strtoul(this_opt + 4, NULL, 0);
		} else if(!strnicmp(this_opt, "pdc:", 4)) {
		        sisfb_pdc = simple_strtoul(this_opt + 4, NULL, 0);
		} else if(!strnicmp(this_opt, "pdc1:", 5)) {
		        sisfb_pdca = simple_strtoul(this_opt + 5, NULL, 0);
		} else if(!strnicmp(this_opt, "noaccel", 7)) {
			sisfb_accel = 0;
		} else if(!strnicmp(this_opt, "accel", 5)) {
			sisfb_accel = -1;
		} else if(!strnicmp(this_opt, "noypan", 6)) {
		        sisfb_ypan = 0;
		} else if(!strnicmp(this_opt, "ypan", 4)) {
		        sisfb_ypan = -1;
		} else if(!strnicmp(this_opt, "nomax", 5)) {
		        sisfb_max = 0;
		} else if(!strnicmp(this_opt, "max", 3)) {
		        sisfb_max = -1;
		} else if(!strnicmp(this_opt, "userom:", 7)) {
			sisfb_userom = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if(!strnicmp(this_opt, "useoem:", 7)) {
			sisfb_useoem = (int)simple_strtoul(this_opt + 7, NULL, 0);
		} else if(!strnicmp(this_opt, "nocrt2rate", 10)) {
			sisfb_nocrt2rate = 1;
	 	} else if(!strnicmp(this_opt, "scalelcd:", 9)) {
		        unsigned long temp = 2;
		        temp = simple_strtoul(this_opt + 9, NULL, 0);
		        if((temp == 0) || (temp == 1)) {
			   sisfb_scalelcd = temp ^ 1;
		        }
		} else if(!strnicmp(this_opt, "tvxposoffset:", 13)) {
		        int temp = 0;
		        temp = (int)simple_strtol(this_opt + 13, NULL, 0);
		        if((temp >= -32) && (temp <= 32)) {
			   sisfb_tvxposoffset = temp;
		        }
		} else if(!strnicmp(this_opt, "tvyposoffset:", 13)) {
		        int temp = 0;
		        temp = (int)simple_strtol(this_opt + 13, NULL, 0);
		        if((temp >= -32) && (temp <= 32)) {
			   sisfb_tvyposoffset = temp;
		        }
		} else if(!strnicmp(this_opt, "specialtiming:", 14)) {
			sisfb_search_specialtiming(this_opt + 14);
		} else if(!strnicmp(this_opt, "lvdshl:", 7)) {
		        int temp = 4;
		        temp = simple_strtoul(this_opt + 7, NULL, 0);
		        if((temp >= 0) && (temp <= 3)) {
			   sisfb_lvdshl = temp;
		        }
		} else if(this_opt[0] >= '0' && this_opt[0] <= '9') {
			sisfb_search_mode(this_opt, TRUE);
#if !defined(__i386__) && !defined(__x86_64__)
	        } else if(!strnicmp(this_opt, "resetcard", 9)) {
		  	sisfb_resetcard = 1;
	        } else if(!strnicmp(this_opt, "videoram:", 9)) {
		  	sisfb_videoram = simple_strtoul(this_opt + 9, NULL, 0);
#endif
		} else {
			printk(KERN_INFO "sisfb: Invalid option %s\n", this_opt);
		}

	}



	return 0;
}
#endif

static UCHAR * __devinit sis_find_rom(struct pci_dev *pdev)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	USHORT pciid;
	int    romptr;
	UCHAR  *myrombase;
	u32    temp;
	SIS_IOTYPE1 *rom_base, *rom;

	if(!(myrombase = vmalloc(65536))) return NULL;

#if defined(__i386__) || defined(__x86_64__)

        for(temp = 0x000c0000; temp < 0x000f0000; temp += 0x00001000) {

            rom_base = ioremap(temp, 0x10000);
	    if(!rom_base) continue;

	    if((readb(rom_base) != 0x55) || (readb(rom_base + 1) != 0xaa)) {
	       iounmap(rom_base);
               continue;
	    }

	    romptr = (unsigned short)(readb(rom_base + 0x18) | (readb(rom_base + 0x19) << 8));
	    if(romptr > (0x10000 - 8)) {
	       iounmap(rom_base);
	       continue;
	    }

	    rom = rom_base + romptr;

	    if((readb(rom)     != 'P') || (readb(rom + 1) != 'C') ||
	       (readb(rom + 2) != 'I') || (readb(rom + 3) != 'R')) {
	       iounmap(rom_base);
	       continue;
	    }

	    pciid = readb(rom + 4) | (readb(rom + 5) << 8);
	    if(pciid != 0x1039) {
	       iounmap(rom_base);
	       continue;
	    }

	    pciid = readb(rom + 6) | (readb(rom + 7) << 8);
	    if(pciid == ivideo->chip_id) {
	       memcpy_fromio(myrombase, rom_base, 65536);
	       iounmap(rom_base);
	       return myrombase;
	    }

	    iounmap(rom_base);
        }

#else

	pci_read_config_dword(pdev, PCI_ROM_ADDRESS, &temp);
	pci_write_config_dword(pdev, PCI_ROM_ADDRESS,
			(ivideo->video_base & PCI_ROM_ADDRESS_MASK) | PCI_ROM_ADDRESS_ENABLE);

	rom_base = ioremap(ivideo->video_base, 65536);
	if(rom_base) {
	   if((readb(rom_base) == 0x55) && (readb(rom_base + 1) == 0xaa)) {
	      romptr = (u16)(readb(rom_base + 0x18) | (readb(rom_base + 0x19) << 8));
	      if(romptr <= (0x10000 - 8)) {
	         rom = rom_base + romptr;
		 if((readb(rom)     == 'P') && (readb(rom + 1) == 'C') &&
		    (readb(rom + 2) == 'I') && (readb(rom + 3) == 'R')) {
		    pciid = readb(rom + 4) | (readb(rom + 5) << 8);
		    if(pciid == 0x1039) {
		       pciid = readb(rom + 6) | (readb(rom + 7) << 8);
		       if(pciid == ivideo->chip_id) {
			  memcpy_fromio(myrombase, rom_base, 65536);
			  iounmap(rom_base);
			  pci_write_config_dword(pdev, PCI_ROM_ADDRESS, temp);
			  return myrombase;
		       }
		    }
		 }
	      }
	   }
	   iounmap(rom_base);
	}
        pci_write_config_dword(pdev, PCI_ROM_ADDRESS, temp);

#endif

       	vfree(myrombase);
        return NULL;
}

#ifdef CONFIG_FB_SIS_300
static int __devinit
sisfb_chkbuswidth300(struct pci_dev *pdev, SIS_IOTYPE1 *FBAddress)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	int i, j;
	USHORT temp;
	UCHAR reg;

	andSISIDXREG(SISSR,0x15,0xFB);
	orSISIDXREG(SISSR,0x15,0x04);
   	outSISIDXREG(SISSR,0x13,0x00);
   	outSISIDXREG(SISSR,0x14,0xBF);

	for(i=0; i<2; i++) {
	   temp = 0x1234;
	   for(j=0; j<4; j++) {
	      writew(temp, FBAddress);
	      if(readw(FBAddress) == temp) break;
	      orSISIDXREG(SISSR,0x3c,0x01);
	      inSISIDXREG(SISSR,0x05,reg);
	      inSISIDXREG(SISSR,0x05,reg);
	      andSISIDXREG(SISSR,0x3c,0xfe);
	      inSISIDXREG(SISSR,0x05,reg);
	      inSISIDXREG(SISSR,0x05,reg);
	      temp++;
	   }
	}

	writel(0x01234567L, FBAddress);
	writel(0x456789ABL, (FBAddress+4));
	writel(0x89ABCDEFL, (FBAddress+8));
	writel(0xCDEF0123L, (FBAddress+12));
	inSISIDXREG(SISSR,0x3b,reg);
	if(reg & 0x01) {
	   if(readl((FBAddress+12)) == 0xCDEF0123L) return(4);  /* Channel A 128bit */
	}
	if(readl((FBAddress+4)) == 0x456789ABL)     return(2);  /* Channel B 64bit */
	return(1);						/* 32bit */
}

static void __devinit
sisfb_setramsize300(struct pci_dev *pdev)
{
	struct  sis_video_info *ivideo = pci_get_drvdata(pdev);
  	SIS_IOTYPE1 *FBAddr = ivideo->video_vbase;
	SIS_IOTYPE1 *Addr;
	USHORT 	sr13, sr14=0, buswidth, Done, data, TotalCapacity, PhysicalAdrOtherPage=0;
	int     PseudoRankCapacity, PseudoTotalCapacity, PseudoAdrPinCount;
   	int     RankCapacity, AdrPinCount, BankNumHigh, BankNumMid, MB2Bank;
   	int     PageCapacity, PhysicalAdrHigh, PhysicalAdrHalfPage, i, j, k;
	const 	USHORT SiS_DRAMType[17][5] = {
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

        buswidth = sisfb_chkbuswidth300(pdev, FBAddr);

   	MB2Bank = 16;
   	Done = 0;
   	for(i = 6; i >= 0; i--) {
      	   if(Done) break;
      	   PseudoRankCapacity = 1 << i;
      	   for(j = 4; j >= 1; j--) {
              if(Done) break;
              PseudoTotalCapacity = PseudoRankCapacity * j;
              PseudoAdrPinCount = 15 - j;
              if(PseudoTotalCapacity <= 64) {
                 for(k = 0; k <= 16; k++) {
                    if(Done) break;
                    RankCapacity = buswidth * SiS_DRAMType[k][3];
                    AdrPinCount = SiS_DRAMType[k][2] + SiS_DRAMType[k][0];
                    if(RankCapacity == PseudoRankCapacity)
                       if(AdrPinCount <= PseudoAdrPinCount) {
                          if(j == 3) {             /* Rank No */
                             BankNumHigh = RankCapacity * MB2Bank * 3 - 1;
                             BankNumMid  = RankCapacity * MB2Bank * 1 - 1;
                          } else {
                             BankNumHigh = RankCapacity * MB2Bank * j - 1;
                             BankNumMid  = RankCapacity * MB2Bank * j / 2 - 1;
                          }
                          PageCapacity = (1 << SiS_DRAMType[k][1]) * buswidth * 4;
                          PhysicalAdrHigh = BankNumHigh;
                          PhysicalAdrHalfPage = (PageCapacity / 2 + PhysicalAdrHigh) % PageCapacity;
                          PhysicalAdrOtherPage = PageCapacity * SiS_DRAMType[k][2] + PhysicalAdrHigh;
                          /* Write data */
                          andSISIDXREG(SISSR,0x15,0xFB); /* Test */
                          orSISIDXREG(SISSR,0x15,0x04);  /* Test */
                          TotalCapacity = SiS_DRAMType[k][3] * buswidth;
                          sr13 = SiS_DRAMType[k][4];
                          if(buswidth == 4) sr14 = (TotalCapacity - 1) | 0x80;
                          if(buswidth == 2) sr14 = (TotalCapacity - 1) | 0x40;
                          if(buswidth == 1) sr14 = (TotalCapacity - 1) | 0x00;
                          outSISIDXREG(SISSR,0x13,sr13);
                          outSISIDXREG(SISSR,0x14,sr14);
                          Addr = FBAddr + BankNumHigh * 64 * 1024 + PhysicalAdrHigh;
                          /* *((USHORT *)(Addr)) = (USHORT)PhysicalAdrHigh; */
			  writew(((USHORT)PhysicalAdrHigh), Addr);
                          Addr = FBAddr + BankNumMid * 64 * 1024 + PhysicalAdrHigh;
                          /* *((USHORT *)(Addr)) = (USHORT)BankNumMid; */
			  writew(((USHORT)BankNumMid), Addr);
                          Addr = FBAddr + BankNumHigh * 64 * 1024 + PhysicalAdrHalfPage;
                          /* *((USHORT *)(Addr)) = (USHORT)PhysicalAdrHalfPage; */
			  writew(((USHORT)PhysicalAdrHalfPage), Addr);
                          Addr = FBAddr + BankNumHigh * 64 * 1024 + PhysicalAdrOtherPage;
                          /* *((USHORT *)(Addr)) = PhysicalAdrOtherPage; */
			  writew(((USHORT)PhysicalAdrOtherPage), Addr);
                          /* Read data */
                          Addr = FBAddr + BankNumHigh * 64 * 1024 + PhysicalAdrHigh;
                          data = readw(Addr); /* *((USHORT *)(Addr)); */
                          if(data == PhysicalAdrHigh) Done = 1;
                       }  /* if */
                 }  /* for k */
              }  /* if */
      	   }  /* for j */
   	}  /* for i */
}

static void __devinit sisfb_post_sis300(struct pci_dev *pdev)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	u8  reg, v1, v2, v3, v4, v5, v6, v7, v8;
	u16 index, rindex, memtype = 0;

	outSISIDXREG(SISSR,0x05,0x86);

	if(ivideo->sishw_ext.UseROM) {
	   if(ivideo->sishw_ext.pjVirtualRomBase[0x52] & 0x80) {
	      memtype = ivideo->sishw_ext.pjVirtualRomBase[0x52];
 	   } else {
	      inSISIDXREG(SISSR,0x3a,memtype);
	   }
	   memtype &= 0x07;
	}

	if(ivideo->revision_id <= 0x13) {
	   v1 = 0x44; v2 = 0x42; v3 = 0x80;
	   v4 = 0x44; v5 = 0x42; v6 = 0x80;
	} else {
	   v1 = 0x68; v2 = 0x43; v3 = 0x80;  /* Assume 125Mhz MCLK */
	   v4 = 0x68; v5 = 0x43; v6 = 0x80;  /* Assume 125Mhz ECLK */
	   if(ivideo->sishw_ext.UseROM) {
	      index = memtype * 5;
	      rindex = index + 0x54;
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	      v2 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	      v3 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	      rindex = index + 0x7c;
	      v4 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	      v5 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	      v6 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	   }
	}
	outSISIDXREG(SISSR,0x28,v1);
	outSISIDXREG(SISSR,0x29,v2);
	outSISIDXREG(SISSR,0x2a,v3);
	outSISIDXREG(SISSR,0x2e,v4);
	outSISIDXREG(SISSR,0x2f,v5);
	outSISIDXREG(SISSR,0x30,v6);
	v1 = 0x10;
	if(ivideo->sishw_ext.UseROM) v1 = ivideo->sishw_ext.pjVirtualRomBase[0xa4];
	outSISIDXREG(SISSR,0x07,v1);       /* DAC speed */
	outSISIDXREG(SISSR,0x11,0x0f);     /* DDC, power save */
	v1 = 0x01; v2 = 0x43; v3 = 0x1e; v4 = 0x2a;
	v5 = 0x06; v6 = 0x00; v7 = 0x00; v8 = 0x00;
	if(ivideo->sishw_ext.UseROM) {
	   memtype += 0xa5;
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[memtype];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 8];
	   v3 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 16];
	   v4 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 24];
	   v5 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 32];
	   v6 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 40];
	   v7 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 48];
	   v8 = ivideo->sishw_ext.pjVirtualRomBase[memtype + 56];
	}
	if(ivideo->revision_id >= 0x80) v3 &= 0xfd;
	outSISIDXREG(SISSR,0x15,v1);       /* Ram type (assuming 0, BIOS 0xa5 step 8) */
	outSISIDXREG(SISSR,0x16,v2);
	outSISIDXREG(SISSR,0x17,v3);
	outSISIDXREG(SISSR,0x18,v4);
	outSISIDXREG(SISSR,0x19,v5);
	outSISIDXREG(SISSR,0x1a,v6);
	outSISIDXREG(SISSR,0x1b,v7);
	outSISIDXREG(SISSR,0x1c,v8);	   /* ---- */
	andSISIDXREG(SISSR,0x15,0xfb);
	orSISIDXREG(SISSR,0x15,0x04);
	if(ivideo->sishw_ext.UseROM) {
	   if(ivideo->sishw_ext.pjVirtualRomBase[0x53] & 0x02) {
	      orSISIDXREG(SISSR,0x19,0x20);
	   }
	}
	v1 = 0x04;			   /* DAC pedestal (BIOS 0xe5) */
	if(ivideo->revision_id >= 0x80) v1 |= 0x01;
	outSISIDXREG(SISSR,0x1f,v1);
	outSISIDXREG(SISSR,0x20,0xa0);     /* linear & relocated io */
	v1 = 0xf6; v2 = 0x0d; v3 = 0x00;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0xe8];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[0xe9];
	   v3 = ivideo->sishw_ext.pjVirtualRomBase[0xea];
	}
	outSISIDXREG(SISSR,0x23,v1);
	outSISIDXREG(SISSR,0x24,v2);
	outSISIDXREG(SISSR,0x25,v3);
	outSISIDXREG(SISSR,0x21,0x84);
	outSISIDXREG(SISSR,0x22,0x00);
	outSISIDXREG(SISCR,0x37,0x00);
	orSISIDXREG(SISPART1,0x24,0x01);   /* unlock crt2 */
	outSISIDXREG(SISPART1,0x00,0x00);
	v1 = 0x40; v2 = 0x11;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0xec];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[0xeb];
	}
	outSISIDXREG(SISPART1,0x02,v1);
	if(ivideo->revision_id >= 0x80) v2 &= ~0x01;
	inSISIDXREG(SISPART4,0x00,reg);
	if((reg == 1) || (reg == 2)) {
	   outSISIDXREG(SISCR,0x37,0x02);
	   outSISIDXREG(SISPART2,0x00,0x1c);
	   v4 = 0x00; v5 = 0x00; v6 = 0x10;
	   if(ivideo->sishw_ext.UseROM) {
	      v4 = ivideo->sishw_ext.pjVirtualRomBase[0xf5];
	      v5 = ivideo->sishw_ext.pjVirtualRomBase[0xf6];
	      v6 = ivideo->sishw_ext.pjVirtualRomBase[0xf7];
	   }
	   outSISIDXREG(SISPART4,0x0d,v4);
	   outSISIDXREG(SISPART4,0x0e,v5);
	   outSISIDXREG(SISPART4,0x10,v6);
	   outSISIDXREG(SISPART4,0x0f,0x3f);
	   inSISIDXREG(SISPART4,0x01,reg);
	   if(reg >= 0xb0) {
	      inSISIDXREG(SISPART4,0x23,reg);
	      reg &= 0x20;
	      reg <<= 1;
	      outSISIDXREG(SISPART4,0x23,reg);
	   }
	} else {
	   v2 &= ~0x10;
	}
	outSISIDXREG(SISSR,0x32,v2);
	andSISIDXREG(SISPART1,0x24,0xfe);  /* Lock CRT2 */
	inSISIDXREG(SISSR,0x16,reg);
	reg &= 0xc3;
	outSISIDXREG(SISCR,0x35,reg);
	outSISIDXREG(SISCR,0x83,0x00);
#if !defined(__i386__) && !defined(__x86_64__)
	if(sisfb_videoram) {
	   outSISIDXREG(SISSR,0x13,0x28);  /* ? */
	   reg = ((sisfb_videoram >> 10) - 1) | 0x40;
	   outSISIDXREG(SISSR,0x14,reg);
	} else {
#endif
	   /* Need to map max FB size for finding out about RAM size */
	   ivideo->video_vbase = ioremap(ivideo->video_base, 0x4000000);
	   if(ivideo->video_vbase) {
	      sisfb_setramsize300(pdev);
	      iounmap(ivideo->video_vbase);
	   } else {
	      printk(KERN_DEBUG "sisfb: Failed to map memory for size detection, assuming 8MB\n");
	      outSISIDXREG(SISSR,0x13,0x28);  /* ? */
	      outSISIDXREG(SISSR,0x14,0x47);  /* 8MB, 64bit default */
	   }
#if !defined(__i386__) && !defined(__x86_64__)
	}
#endif
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0xe6];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[0xe7];
	} else {
	   inSISIDXREG(SISSR,0x3a,reg);
	   if((reg & 0x30) == 0x30) {
	      v1 = 0x04; /* PCI */
	      v2 = 0x92;
	   } else {
	      v1 = 0x14; /* AGP */
	      v2 = 0xb2;
	   }
	}
	outSISIDXREG(SISSR,0x21,v1);
	outSISIDXREG(SISSR,0x22,v2);
}
#endif

#ifdef CONFIG_FB_SIS_315
static void __devinit sisfb_post_sis315330(struct pci_dev *pdev)
{
#ifdef YET_TO_BE_DONE
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	u8  reg, v1, v2, v3, v4, v5, v6, v7, v8;
	u16 index, rindex, memtype = 0;
	u32 reg1_32, reg2_32, reg3_32;
	int i;

	/* Unlock */
	/* outSISIDXREG(0x3c4,0x05,0x86); */
	outSISIDXREG(SISSR,0x05,0x86);

	/* Enable relocated i/o ports */
	/* setSISIDXREG(0x3c4,0x20,~0x10,0x20); */
	setSISIDXREG(SISSR,0x20,~0x10,0x20);

	/* Clear regs */
	for(i = 0; i < 0x22; i++) {
	   outSISIDXREG(SISSR,(0x06 + i),0x00);
	}
	v1 = 0x0d;
	if( is 330) v1 = 0x0b;
	for(i = 0; i < v1; i++) {
	   outSISIDXREG(SISSR,(0x31 + i),0x00);
	}
	for(i = 0; i < 0x10; i++) {
	   outSISIDXREG(SISCR,(0x30 + i),0x00);
	}

	/* Reset clocks */
	reg = inSISREG(SISMISCR);
	outSISIDXREG(SISSR,0x28,0x81);
	outSISIDXREG(SISSR,0x2A,0x00);
	outSISIDXREG(SISSR,0x29,0xE1);
	outSISREG(SISMISCW,(reg | 0x0c));
	outSISIDXREG(SISSR,0x2B,0x81);
	outSISIDXREG(SISSR,0x2D,0x00);
	outSISIDXREG(SISSR,0x2C,0xE1);
	outSISIDXREG(SISSR,0x2E,0x81);
	outSISIDXREG(SISSR,0x30,0x00);
	outSISIDXREG(SISSR,0x2F,0xE1);
	SiS_DDC2Delay(....);
	outSISREG(SISMISCW,reg);

	/* Get memory type */
	if(ivideo->sishw_ext.UseROM) {
	   if(ivideo->sishw_ext.pjVirtualRomBase[0x52] & 0x80)) {
	      memtype = ivideo->sishw_ext.pjVirtualRomBase[0x52];
 	   } else {
	      inSISIDXREG(SISSR,0x3a,memtype);
	   }
	   memtype &= 0x03;
	   if( is 330 ) {
	      if(memtype <= 1) memtype = 0;
	      else {
	         inSISIDXREG(SISCR,0x5F,reg);
		 reg &= 0x30;
		 switch(reg) {
		 case 0x00: memtype = 1; break;
		 case 0x10: memtype = 3; break;
		 case 0x20: memtype = 3; break;
		 default:   memtype = 2;
		 }
	      }
	   }
	}

	/* Set clocks */
	v1 = 0x3b; v2 = 0x22; v3 = 0x01;  /* Assume 143Mhz MCLK */
	v4 = 0x5c; v5 = 0x23; v6 = 0x01;  /* Assume 166Mhz ECLK */
	if(ivideo->sishw_ext.UseROM) {
	   index = memtype * 5;
	   rindex = index + 0x54;
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	   v3 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	   rindex = index + 0x68;
	   v4 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	   v5 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	   v6 = ivideo->sishw_ext.pjVirtualRomBase[rindex++];
	}
	outSISIDXREG(SISSR,0x28,v1);
	outSISIDXREG(SISSR,0x29,v2);
	outSISIDXREG(SISSR,0x2a,v3);
	if( is 330 ) {
	   inSISIDXREG(SISSR,0x3a,reg);
	   reg &= 0x03;
	   if(reg >= 2) {
	      ...
	   }
	}
	outSISIDXREG(SISSR,0x2e,v4);
	outSISIDXREG(SISSR,0x2f,v5);
	outSISIDXREG(SISSR,0x30,v6);

	/* End of comp with 330 */

	v1 = 0x18;
	if(ivideo->sishw_ext.UseROM) v1 = ivideo->sishw_ext.pjVirtualRomBase[0x7c];
	outSISIDXREG(SISSR,0x07,v1);
	outSISIDXREG(SISSR,0x11,0x0f);

	v1 = 0x00; v2 = 0x0f; v3 = 0xba; v4 = 0xa9;
	v5 = 0xa0; v6 = 0x00; v7 = 0x30;
	if(ivideo->sishw_ext.UseROM) {
	   index = memtype + 0x7d;
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[index];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[index + 4];
	   v3 = ivideo->sishw_ext.pjVirtualRomBase[index + 8];
	   v4 = ivideo->sishw_ext.pjVirtualRomBase[index + 12];
	   v5 = ivideo->sishw_ext.pjVirtualRomBase[index + 16];
	   v6 = ivideo->sishw_ext.pjVirtualRomBase[index + 20];
	   v7 = ivideo->sishw_ext.pjVirtualRomBase[index + 24];
	}
	outSISIDXREG(SISSR,0x15,v1);       /* Ram type (assuming 0, BIOS 0x7d step 4) */
	outSISIDXREG(SISSR,0x16,v2);
	outSISIDXREG(SISSR,0x17,v3);
	outSISIDXREG(SISSR,0x18,v4);
	outSISIDXREG(SISSR,0x19,v5);
	outSISIDXREG(SISSR,0x1a,v6);
	outSISIDXREG(SISSR,0x1b,v7);
	outSISIDXREG(SISSR,0x1c,v8);	   /* ---- */

	v1 = 0x77; v2 = 0x77; v3 = 0x00; v4 = 0x5b; v5 = 0x00;
	if(ivideo->sishw_ext.UseROM) {
	   index = memtype + 0xa2;
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[index];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[index + 4];
	   v3 = ivideo->sishw_ext.pjVirtualRomBase[index + 8];
	   v4 = ivideo->sishw_ext.pjVirtualRomBase[index + 12];
	   v5 = ivideo->sishw_ext.pjVirtualRomBase[index + 16];
	}
	outSISIDXREG(SISCR,0x40,v1);
	outSISIDXREG(SISCR,0x41,v2);
	outSISIDXREG(SISCR,0x42,v3);
	outSISIDXREG(SISCR,0x43,v4);
	outSISIDXREG(SISCR,0x44,v5);

	if( is 330 ) {

	   v1 = 0x;
	   if(ivideo->sishw_ext.UseROM) {
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[0xBA];
	   }
	   outSISIDXREG(SISCR,0x59,v1);

	   v1 = 0x; v2 = 0x; v3 = 0x; v4 = 0x;
	   v5 = 0x; v6 = 0x; v7 = 0x; v8 = 0x;
	   if(ivideo->sishw_ext.UseROM) {
	      index = memtype + 0xbe;
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[index];
	      v2 = ivideo->sishw_ext.pjVirtualRomBase[index + 4];
	      v3 = ivideo->sishw_ext.pjVirtualRomBase[index + 8];
	      v4 = ivideo->sishw_ext.pjVirtualRomBase[index + 12];
	      v5 = ivideo->sishw_ext.pjVirtualRomBase[index + 16];
	      v6 = ivideo->sishw_ext.pjVirtualRomBase[index + 20];
	      v7 = ivideo->sishw_ext.pjVirtualRomBase[index + 24];
	      v8 = ivideo->sishw_ext.pjVirtualRomBase[index + 28];
	   }
	   outSISIDXREG(SISCR,0x68,v1);
	   outSISIDXREG(SISCR,0x69,v2);
	   outSISIDXREG(SISCR,0x6a,v3);
	   outSISIDXREG(SISCR,0x6b,v4);
	   outSISIDXREG(SISCR,0x6c,v5);
	   outSISIDXREG(SISCR,0x6d,v6);
	   outSISIDXREG(SISCR,0x6e,v7);
	   outSISIDXREG(SISCR,0x6f,v8);

	   v1 = 0x20;
	   inSISIDXREG(SISSR,0x3b,reg);

	   if(!(reg & 0x04)) {
	      inSISIDXREG(SISCR,0x5F,reg);
	      reg &= 0x30;
	      if(reg) v1 = 0x23;
	   }
	   outSISIDXREG(SISCR,0x48,v1);
	   outSISIDXREG(SISCR,0x4c,0x20);

	   xx= xxx();
	   if(xx >= 1) {
	      v1 = 0x;
	      if(ivideo->sishw_ext.UseROM) {
	         v1 = ivideo->sishw_ext.pjVirtualRomBase[0xBA];
	      }
	      outSISIDXREG(SISCR,0x59,v1);
	   }



	} else {

	   outSISIDXREG(SISCR,0x48,0x23);

	   andSISIDXREG(SISSR,0x16,0x0f);
	   if(memtype <= 1) {
	      orSISIDXREG(SISSR,0x16,0x80);
	   } else {
	      v1 = 0x0f;
	      if(ivideo->sishw_ext.UseROM) {
	         v1 = ivideo->sishw_ext.pjVirtualRomBase[0x81 + memtype];
	      }
	      if(!(v1 & 0x10)) v2 = 0xc0;
	      else             v2 = 0xd0;
	      orSISIDXREG(SISSR,0x16,v2);
	      andSISIDXREG(SISSR,0x16,0x0f);
	      if(!(v1 & 0x10)) v2 = 0x80;
	      else             v2 = 0xA0;
	      orSISIDXREG(SISSR,0x16,v2);
 	   }

	   if(memtype >= 2) {
	      const u8 sr3cseq1[] = { 0xc0,0xe0,0xf0,0xe0,0xf0,0xa0,0xb0,0xa0,0xb0,0x90,0xd0 };
	      const u8 sr3cseq2[] = { 0xc0,0xa0,0xb0,0xa0,0xb0,0xe0,0xf0,0xa0,0xb0,0x90,0xd0 };
	      for(i = 0; i < 11; i++) {
	         outSISIDXREG(SISSR,0x3c,sr3cseq1[i]);
	      }
	      outSISIDXREG(SISSR,0x3d,0x00);
	      outSISIDXREG(SISSR,0x3d,0x04);
	      SiS_DDC2Delay(0x200);
	      v1 = inSISIDXREG(SISCR,0xEC);
	      v2 = inSISIDXREG(SISCR,0xED);
	      reg1_32 = (v2 << 8) | v1;
	      outSISIDXREG(SISSR,0x3D,0x00);
	      for(i = 0; i < 11; i++) {
	         outSISIDXREG(SISSR,0x3c,sr3cseq2[i]);
	      }
	      outSISIDXREG(SISSR,0x3d,0x00);
	      outSISIDXREG(SISSR,0x3d,0x04);
	      SiS_DDC2Delay(0x200);
	      v1 = inSISIDXREG(SISCR,0xEC);
	      v2 = inSISIDXREG(SISCR,0xED);
	      reg2_32 = (v2 << 8) | v1;
	      outSISIDXREG(SISSR,0x3D,0x00);
	      reg3_32 = reg2_32 << 1;
	      reg2_32 >>= 1;
	      reg3_32 += reg2_32;
	      v1 = 0x40;
	      if(reg3_32 > reg1_32) v1 = 0x10;
	         outSISIDXREG(SISCR,0x59,v1);
	   }

	}

	v1 = 0x00;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0x99];
	}
	outSISIDXREG(SISSR,0x1f,v1);

	outSISIDXREG(SISSR,0x20,0x20);

	v1 = 0xf6; v2 = 0x0d; v3 = 0x33;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0x9c];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[0x9d];
	   v3 = ivideo->sishw_ext.pjVirtualRomBase[0x9e];
	}
	outSISIDXREG(SISSR,0x23,v1);
	outSISIDXREG(SISSR,0x24,v2);
	outSISIDXREG(SISSR,0x25,v3);

	outSISIDXREG(SISSR,0x21,0x84);
	outSISIDXREG(SISSR,0x22,0x00);
	outSISIDXREG(SISSR,0x27,0x1f);

	v1 = 0x00; v2 = 0x00;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0x9F];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[0xA1];
	}
	outSISIDXREG(SISSR,0x31,v1);
	outSISIDXREG(SISSR,0x33,v2);

	v1 = 0x11;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0xA0];
	}
	v2 = inSISIDXREG(SISPART4,0x00);
	if((v2 != 1) && (v2 != 2)) v1 &= 0xef;
	outSISIDXREG(SISSR,0x32,v1);

	/* AGP */
	pci_read_config_long(pdev, 0x50, &reg1_32);
	reg1_32 >>= 20;
	reg1_32 &= 0x0f;
	if(reg1_32 == 1) {
	   v1 = 0xAA; v2 = 0x33;
	   if(ivideo->sishw_ext.UseROM) {
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[0xF7];
	      v2 = ivideo->sishw_ext.pjVirtualRomBase[0x9E];
	   }
	} else {
	   v1 = 0x88; v2 = 0x03;
	   if(ivideo->sishw_ext.UseROM) {
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[0xF8];
	      v2 = ivideo->sishw_ext.pjVirtualRomBase[0xF6];
	   }
	}
	outSISIDXREG(SISCR,0x49,v1);
	outSISIDXREG(SISSR,0x25,v2);

	v1 = inSISIDXREG(SISPART4,0x00);
	if((v1 == 1) || (v1 == 2)) {
	   orSISIDXREG(SISPART1,0x2F,0x01);  /* Unlock CRT2 */
	   outSISIDXREG(SISPART1,0x00,0x00);
	   v1 = 0x00;
	   if(ivideo->sishw_ext.UseROM) {
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[0xb6];
	   }
	   outSISIDXREG(SISPART1,0x02,v1);
	   outSISIDXREG(SISPART1,0x2E,0x08);
	   outSISIDXREG(SISPART2,0x00,0x1c);
	   v1 = 0x40; v2 = 0x00; v3 = 0x80;
	   if(ivideo->sishw_ext.UseROM) {
	      v1 = ivideo->sishw_ext.pjVirtualRomBase[0xb7];
	      v2 = ivideo->sishw_ext.pjVirtualRomBase[0xb8];
	      v3 = ivideo->sishw_ext.pjVirtualRomBase[0xbb];
	   }
	   outSISIDXREG(SISPART4,0x0d,v1);
	   outSISIDXREG(SISPART4,0x0e,v2);
	   outSISIDXREG(SISPART4,0x10,v3);
	   outSISIDXREG(SISPART4,0x0F,0x3F);

	   inSISIDXREG(SISPART4,0x01,reg);
	   if(reg >= 0xb0) {
	      inSISIDXREG(SISPART4,0x23,reg);
	      reg &= 0x20;
	      reg <<= 1;
	      outSISIDXREG(SISPART4,0x23,reg);
	   }
	}
	outSISIDXREG(SISCR,0x37,0x02); /* Why? */

	outSISIDXREG(SISCR,0x83,0x00);
	outSISIDXREG(SISCR,0x90,0x00);
	andSISIDXREG(SISSR,0x5B,0xDF);
	outSISIDXREG(SISVID,0x00,0x86);
	outSISIDXREG(SISVID,0x32,0x00);
	outSISIDXREG(SISVID,0x30,0x00);
	outSISIDXREG(SISVID,0x32,0x01);
	outSISIDXREG(SISVID,0x30,0x00);
	orSISIDXREG(SISCR,0x63,0x80);
	/* End of Init1 */

	/* Set Mode 0x2e */

	/* Ramsize */
	orSISIDXREG(SISSR,0x16,0x0f);
	orSISIDXREG(SISSR,0x18,0xA9);
	orSISIDXREG(SISSR,0x19,0xA0);
	orSISIDXREG(SISSR,0x1B,0x30);
	andSISIDXREG(SISSR,0x17,0xF8);
	orSISIDXREG(SISSR,0x19,0x03);
	andSIDIDXREG(SISSR,0x13,0x00);

	/* Need to map max FB size for finding out about RAM size */
	ivideo->video_vbase = ioremap(ivideo->video_base, 0x4000000);
	if(ivideo->video_vbase) {
	   /* Find out about bus width */
	   if(memtype <= 1) {
	      outSISIDXREG(SISSR,0x14,0x02);
	      andSISIDXREG(SISSR,0x16,0x0F);
	      orSISIDXREG(SISSR,0x16,0x80);

	      ...

	   } else {

	      ...

	   }

	   /* Find out about size */


	   iounmap(ivideo->video_vbase);
	} else {
	   printk(KERN_DEBUG "sisfb: Failed to map memory for size detection, assuming 8MB\n");
	   outSISIDXREG(SISSR,0x14,0x??);  /* 8MB, 64bit default */
	}

	/* AGP (Missing: Checks for VIA and AMD hosts) */
	v1 = 0xA5; v2 = 0xFB;
	if(ivideo->sishw_ext.UseROM) {
	   v1 = ivideo->sishw_ext.pjVirtualRomBase[0x9A];
	   v2 = ivideo->sishw_ext.pjVirtualRomBase[0x9B];
	}
	outSISIDXREG(SISSR,0x21,v1);
	outSISIDXREG(SISSR,0x22,v2);

#endif
	return;
}
#endif


static int __devinit sisfb_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct sisfb_chip_info 	*chipinfo = &sisfb_chip_info[ent->driver_data];
	struct sis_video_info 	*ivideo = NULL;
	struct fb_info 		*sis_fb_info = NULL;
	u16 reg16;
	u8  reg;
	int sisvga_enabled = 0, i;

	if(sisfb_off) return -ENXIO;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,3))
	sis_fb_info = framebuffer_alloc(sizeof(*ivideo), &pdev->dev);
	if(!sis_fb_info) return -ENOMEM;
#else
	sis_fb_info = kmalloc(sizeof(*sis_fb_info) + sizeof(*ivideo), GFP_KERNEL);
	if(!sis_fb_info) return -ENOMEM;
	memset(sis_fb_info, 0, sizeof(*sis_fb_info) + sizeof(*ivideo));
	sis_fb_info->par = ((char *)sis_fb_info + sizeof(*sis_fb_info));
#endif

	ivideo = (struct sis_video_info *)sis_fb_info->par;
	ivideo->memyselfandi = sis_fb_info;

	if(card_list == NULL) {
	   ivideo->cardnumber = 0;
	} else {
	   struct sis_video_info *countvideo = card_list;
	   ivideo->cardnumber = 1;
	   while((countvideo = countvideo->next) != NULL) ivideo->cardnumber++;
	}

	strncpy(ivideo->myid, chipinfo->chip_name, 30);

	ivideo->warncount = 0;
	ivideo->chip_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &ivideo->revision_id);
	ivideo->sishw_ext.jChipRevision = ivideo->revision_id;
	pci_read_config_word(pdev, PCI_COMMAND, &reg16);
	sisvga_enabled = reg16 & 0x01;
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
	ivideo->sisvga_engine = chipinfo->vgaengine;
	ivideo->hwcursor_size = chipinfo->hwcursor_size;
	ivideo->CRT2_write_enable = chipinfo->CRT2_write_enable;
	ivideo->mni = chipinfo->mni;

	ivideo->detectedpdc  = 0xff;
	ivideo->detectedpdca = 0xff;
	ivideo->detectedlcda = 0xff;

	ivideo->sisfb_thismonitor.datavalid = FALSE;

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
	ivideo->sisfb_filter = sisfb_filter;
	ivideo->sisfb_nocrt2rate = sisfb_nocrt2rate;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)
	ivideo->sisfb_inverse = sisfb_inverse;
#endif

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
        ivideo->SiS_Pr.SiS_ChSW = FALSE;
	ivideo->SiS_Pr.SiS_UseLCDA = FALSE;
	ivideo->SiS_Pr.HaveEMI = FALSE;
	ivideo->SiS_Pr.HaveEMILCD = FALSE;
	ivideo->SiS_Pr.OverruleEMI = FALSE;
	ivideo->SiS_Pr.SiS_SensibleSR11 = FALSE;
	ivideo->SiS_Pr.SiS_MyCR63 = 0x63;
	ivideo->SiS_Pr.PDC  = -1;
	ivideo->SiS_Pr.PDCA = -1;
#ifdef CONFIG_FB_SIS_315
	if(ivideo->chip >= SIS_330) {
	   ivideo->SiS_Pr.SiS_MyCR63 = 0x53;
	   if(ivideo->chip >= SIS_661) {
	      ivideo->SiS_Pr.SiS_SensibleSR11 = TRUE;
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
#endif
		}
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	strcpy(sis_fb_info->modename, ivideo->myid);
#endif

	ivideo->sishw_ext.jChipType = ivideo->chip;

#ifdef CONFIG_FB_SIS_315
	if((ivideo->sishw_ext.jChipType == SIS_315PRO) ||
	   (ivideo->sishw_ext.jChipType == SIS_315)) {
		ivideo->sishw_ext.jChipType = SIS_315H;
	}
#endif

	ivideo->video_base = pci_resource_start(pdev, 0);
	ivideo->mmio_base  = pci_resource_start(pdev, 1);
	ivideo->mmio_size  = pci_resource_len(pdev, 1);
	ivideo->SiS_Pr.RelIO = pci_resource_start(pdev, 2) + 0x30;
	ivideo->sishw_ext.ulIOAddress = ivideo->vga_base = ivideo->SiS_Pr.RelIO;

	if(!sisvga_enabled) {
	   	if(pci_enable_device(pdev)) {
	      		pci_set_drvdata(pdev, NULL);
	      		kfree(sis_fb_info);
	      		return -EIO;
	   	}
	}

	SiSRegInit(&ivideo->SiS_Pr, ivideo->sishw_ext.ulIOAddress);

#ifdef CONFIG_FB_SIS_300
	/* Find PCI systems for Chrontel/GPIO communication setup */
	if(ivideo->chip == SIS_630) {
	   i=0;
           do {
	      if(mychswtable[i].subsysVendor == ivideo->subsysvendor &&
	         mychswtable[i].subsysCard   == ivideo->subsysdevice) {
		 ivideo->SiS_Pr.SiS_ChSW = TRUE;
		 printk(KERN_DEBUG "sisfb: Identified [%s %s] requiring Chrontel/GPIO setup\n",
		        mychswtable[i].vendorName, mychswtable[i].cardName);
		 break;
              }
              i++;
           } while(mychswtable[i].subsysVendor != 0);
	}
#endif

        outSISIDXREG(SISSR, 0x05, 0x86);

	if( (!sisvga_enabled)
#if !defined(__i386__) && !defined(__x86_64__)
		  	      || (sisfb_resetcard)
#endif
			      			   ) {
	   	for(i = 0x30; i <= 0x3f; i++) {
	      		outSISIDXREG(SISCR,i,0x00);
	   	}
	}

	/* Find out about current video mode */
	ivideo->modeprechange = 0x03;
	inSISIDXREG(SISCR,0x34,reg);
	if(reg & 0x7f) {
		ivideo->modeprechange = reg & 0x7f;
	} else if(sisvga_enabled) {
#if defined(__i386__) || defined(__x86_64__)
		unsigned char SIS_IOTYPE2 *tt = ioremap(0, 0x1000);
		if(tt) {
		   	ivideo->modeprechange = readb(tt + 0x449);
		   	iounmap(tt);
		}
#endif
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#ifdef MODULE
	if((reg & 0x80) && (reg != 0xff)) {
	   if((sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni]) != 0xFF) {
	      printk(KERN_INFO "sisfb: Cannot initialize display mode, X server is active\n");
	      pci_set_drvdata(pdev, NULL);
	      kfree(sis_fb_info);
	      return -EBUSY;
	   }
	}
#endif	
#endif

	ivideo->sishw_ext.bIntegratedMMEnabled = TRUE;
#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
	   if(ivideo->chip != SIS_300) {
	      inSISIDXREG(SISSR, 0x1a, reg);
	      if(!(reg & 0x10)) {
		 ivideo->sishw_ext.bIntegratedMMEnabled = FALSE;
	      }
	   }
	}
#endif

	ivideo->bios_abase = NULL;
	if(ivideo->sisfb_userom) {
	    ivideo->sishw_ext.pjVirtualRomBase = sis_find_rom(pdev);
	    ivideo->bios_abase = ivideo->sishw_ext.pjVirtualRomBase;
	    if(ivideo->sishw_ext.pjVirtualRomBase) {
		printk(KERN_INFO "sisfb: Video ROM found and copied\n");
		ivideo->sishw_ext.UseROM = TRUE;
	    } else {
	        ivideo->sishw_ext.UseROM = FALSE;
	        printk(KERN_INFO "sisfb: Video ROM not found\n");
	    }
	} else {
	    ivideo->sishw_ext.pjVirtualRomBase = NULL;
	    ivideo->sishw_ext.UseROM = FALSE;
	    printk(KERN_INFO "sisfb: Video ROM usage disabled\n");
	}

        /* Find systems for special custom timing */
	if(ivideo->SiS_Pr.SiS_CustomT == CUT_NONE) {
	   int j;
	   unsigned char *biosver = NULL;
           unsigned char *biosdate = NULL;
	   BOOLEAN footprint;
	   u32 chksum = 0;

	   if(ivideo->sishw_ext.UseROM) {
	      biosver = ivideo->sishw_ext.pjVirtualRomBase + 0x06;
	      biosdate = ivideo->sishw_ext.pjVirtualRomBase + 0x2c;
              for(i=0; i<32768; i++) chksum += ivideo->sishw_ext.pjVirtualRomBase[i];
	   }

	   i=0;
           do {
	      if( (mycustomttable[i].chipID == ivideo->chip) &&
	          ((!strlen(mycustomttable[i].biosversion)) ||
		   (ivideo->sishw_ext.UseROM &&
		   (!strncmp(mycustomttable[i].biosversion, biosver, strlen(mycustomttable[i].biosversion))))) &&
	          ((!strlen(mycustomttable[i].biosdate)) ||
		   (ivideo->sishw_ext.UseROM &&
		   (!strncmp(mycustomttable[i].biosdate, biosdate, strlen(mycustomttable[i].biosdate))))) &&
		  ((!mycustomttable[i].bioschksum) ||
		   (ivideo->sishw_ext.UseROM &&
	           (mycustomttable[i].bioschksum == chksum)))	&&
		  (mycustomttable[i].pcisubsysvendor == ivideo->subsysvendor) &&
		  (mycustomttable[i].pcisubsyscard == ivideo->subsysdevice) ) {
		 footprint = TRUE;
	         for(j = 0; j < 5; j++) {
	            if(mycustomttable[i].biosFootprintAddr[j]) {
		       if(ivideo->sishw_ext.UseROM) {
	                  if(ivideo->sishw_ext.pjVirtualRomBase[mycustomttable[i].biosFootprintAddr[j]] !=
		      		mycustomttable[i].biosFootprintData[j]) {
		             footprint = FALSE;
			  }
		       } else footprint = FALSE;
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

#ifdef CONFIG_FB_SIS_300
	if(ivideo->sisvga_engine == SIS_300_VGA) {
		if( (!sisvga_enabled)
#if !defined(__i386__) && !defined(__x86_64__)
		    		      || (sisfb_resetcard)
#endif
		  					   ) {
			if(ivideo->chip == SIS_300) {
				sisfb_post_sis300(pdev);
			}
		}
	}
#endif

#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
		if( (!sisvga_enabled)
#if !defined(__i386__) && !defined(__x86_64__)
		    		     || (sisfb_resetcard)
#endif
		  					  ) {
			if((ivideo->chip == SIS_315H)   ||
			   (ivideo->chip == SIS_315)    ||
			   (ivideo->chip == SIS_315PRO) ||
			   (ivideo->chip == SIS_330)) {
				sisfb_post_sis315330(pdev);
			}
		}
	}
#endif

	if(sisfb_get_dram_size(ivideo)) {
		printk(KERN_INFO "sisfb: Fatal error: Unable to determine RAM size.\n");
		if(ivideo->bios_abase) vfree(ivideo->bios_abase);
		pci_set_drvdata(pdev, NULL);
		kfree(sis_fb_info);
		return -ENODEV;
	}

	if((ivideo->sisfb_mode_idx < 0) ||
	   ((sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni]) != 0xFF)) {
	        /* Enable PCI_LINEAR_ADDRESSING and MMIO_ENABLE  */
	        orSISIDXREG(SISSR, IND_SIS_PCI_ADDRESS_SET, (SIS_PCI_ADDR_ENABLE | SIS_MEM_MAP_IO_ENABLE));
                /* Enable 2D accelerator engine */
	        orSISIDXREG(SISSR, IND_SIS_MODULE_ENABLE, SIS_ENABLE_2D);
	}

	if(sisfb_pdc != 0xff) {
	   if(ivideo->sisvga_engine == SIS_300_VGA) sisfb_pdc &= 0x3c;
	   else				            sisfb_pdc &= 0x1f;
	   ivideo->SiS_Pr.PDC = sisfb_pdc;
	}
#ifdef CONFIG_FB_SIS_315
	if(ivideo->sisvga_engine == SIS_315_VGA) {
	   if(sisfb_pdca != 0xff) ivideo->SiS_Pr.PDCA = sisfb_pdca & 0x1f;
	}
#endif

	if(!request_mem_region(ivideo->video_base, ivideo->video_size, "sisfb FB")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve frame buffer memory\n");
		printk(KERN_ERR "sisfb: Is there another framebuffer driver active?\n");
		if(ivideo->bios_abase) vfree(ivideo->bios_abase);
		pci_set_drvdata(pdev, NULL);
		kfree(sis_fb_info);
		return -ENODEV;
	}

	if(!request_mem_region(ivideo->mmio_base, ivideo->mmio_size, "sisfb MMIO")) {
		printk(KERN_ERR "sisfb: Fatal error: Unable to reserve MMIO region\n");
		release_mem_region(ivideo->video_base, ivideo->video_size);
		if(ivideo->bios_abase) vfree(ivideo->bios_abase);
		pci_set_drvdata(pdev, NULL);
		kfree(sis_fb_info);
		return -ENODEV;
	}

	ivideo->video_vbase = ioremap(ivideo->video_base, ivideo->video_size);
	ivideo->sishw_ext.pjVideoMemoryAddress = ivideo->video_vbase;
	if(!ivideo->video_vbase) {
	   	printk(KERN_ERR "sisfb: Fatal error: Unable to map frame buffer memory\n");
	   	release_mem_region(ivideo->video_base, ivideo->video_size);
	   	release_mem_region(ivideo->mmio_base, ivideo->mmio_size);
		if(ivideo->bios_abase) vfree(ivideo->bios_abase);
		pci_set_drvdata(pdev, NULL);
	   	kfree(sis_fb_info);
	   	return -ENODEV;
	}

	ivideo->mmio_vbase = ioremap(ivideo->mmio_base, ivideo->mmio_size);
	if(!ivideo->mmio_vbase) {
	   	printk(KERN_ERR "sisfb: Fatal error: Unable to map MMIO region\n");
	   	iounmap(ivideo->video_vbase);
	   	release_mem_region(ivideo->video_base, ivideo->video_size);
	   	release_mem_region(ivideo->mmio_base, ivideo->mmio_size);
		if(ivideo->bios_abase) vfree(ivideo->bios_abase);
		pci_set_drvdata(pdev, NULL);
	   	kfree(sis_fb_info);
	   	return -ENODEV;
	}

	printk(KERN_INFO "sisfb: Framebuffer at 0x%lx, mapped to 0x%lx, size %ldk\n",
	       	ivideo->video_base, (ULONG)ivideo->video_vbase, ivideo->video_size / 1024);

	printk(KERN_INFO "sisfb: MMIO at 0x%lx, mapped to 0x%lx, size %ldk\n",
	       	ivideo->mmio_base, (ULONG)ivideo->mmio_vbase, ivideo->mmio_size / 1024);

	if((ivideo->havenoheap = sisfb_heap_init(ivideo))) {
		printk(KERN_WARNING "sisfb: Failed to initialize offscreen memory heap\n");
	}

	/* Used for clearing the screen only, therefore respect our mem limit */
	ivideo->sishw_ext.ulVideoMemorySize = ivideo->sisfb_mem;

	ivideo->mtrr = 0;

	ivideo->vbflags = 0;
	ivideo->lcddefmodeidx = DEFAULT_LCDMODE;
	ivideo->tvdefmodeidx  = DEFAULT_TVMODE;
	ivideo->defmodeidx    = DEFAULT_MODE;

	ivideo->newrom = SiSDetermineROMLayout661(&ivideo->SiS_Pr, &ivideo->sishw_ext);

	if((ivideo->sisfb_mode_idx < 0) ||
	   ((sisbios_mode[ivideo->sisfb_mode_idx].mode_no[ivideo->mni]) != 0xFF)) {

		sisfb_sense_crt1(ivideo);

		sisfb_get_VB_type(ivideo);

		if(ivideo->vbflags & VB_VIDEOBRIDGE) {
			sisfb_detect_VB_connect(ivideo);
		}

		ivideo->currentvbflags = ivideo->vbflags & (VB_VIDEOBRIDGE | TV_STANDARD);

		if(ivideo->vbflags & VB_VIDEOBRIDGE) {
		   if(ivideo->sisfb_crt2type != -1) {
		      if((ivideo->sisfb_crt2type == CRT2_LCD) && (ivideo->vbflags & CRT2_LCD)) {
		         ivideo->currentvbflags |= CRT2_LCD;
		      } else if(ivideo->sisfb_crt2type != CRT2_LCD) {
		         ivideo->currentvbflags |= ivideo->sisfb_crt2type;
		      }
		   } else {
		      /* Chrontel 700x TV detection often unreliable, therefore use a
		       * different default order on such machines
		       */
		      if((ivideo->sisvga_engine == SIS_300_VGA) && (ivideo->vbflags & VB_CHRONTEL)) {
		         if(ivideo->vbflags & CRT2_LCD)      ivideo->currentvbflags |= CRT2_LCD;
		         else if(ivideo->vbflags & CRT2_TV)  ivideo->currentvbflags |= CRT2_TV;
		         else if(ivideo->vbflags & CRT2_VGA) ivideo->currentvbflags |= CRT2_VGA;
		      } else {
		         if(ivideo->vbflags & CRT2_TV)       ivideo->currentvbflags |= CRT2_TV;
		         else if(ivideo->vbflags & CRT2_LCD) ivideo->currentvbflags |= CRT2_LCD;
		         else if(ivideo->vbflags & CRT2_VGA) ivideo->currentvbflags |= CRT2_VGA;
		      }
		   }
		}

		if(ivideo->vbflags & CRT2_LCD) {
		   inSISIDXREG(SISCR, 0x36, reg);
		   reg &= 0x0f;
		   if(ivideo->sisvga_engine == SIS_300_VGA) {
		      ivideo->CRT2LCDType = sis300paneltype[reg];
		   } else if(ivideo->chip >= SIS_661) {
		      ivideo->CRT2LCDType = sis661paneltype[reg];
		   } else {
		      ivideo->CRT2LCDType = sis310paneltype[reg];
		      if((ivideo->chip == SIS_550) && (sisfb_fstn)) {
		         if((ivideo->CRT2LCDType != LCD_640x480_2) &&
			    (ivideo->CRT2LCDType != LCD_640x480_3)) {
		      	    ivideo->CRT2LCDType = LCD_320x480;
			 }
		      }
		   }
		   if(ivideo->CRT2LCDType == LCD_UNKNOWN) {
		      /* For broken BIOSes: Assume 1024x768, RGB18 */
		      ivideo->CRT2LCDType = LCD_1024x768;
		      setSISIDXREG(SISCR,0x36,0xf0,0x02);
		      setSISIDXREG(SISCR,0x37,0xee,0x01);
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
		   if(ivideo->SiS_Pr.SiS_CustomT == CUT_BARCO1366) {
	   		ivideo->lcdxres = 1360; ivideo->lcdyres = 1024; ivideo->lcddefmodeidx = 99;
		   } else if(ivideo->SiS_Pr.SiS_CustomT == CUT_PANEL848) {
	   		ivideo->lcdxres =  848; ivideo->lcdyres =  480; ivideo->lcddefmodeidx = 47;
		   }
		   printk(KERN_DEBUG "sisfb: Detected %dx%d flat panel\n",
		   		ivideo->lcdxres, ivideo->lcdyres);
		}

#ifdef CONFIG_FB_SIS_300
                /* Save the current PanelDelayCompensation if the LCD is currently used */
		if(ivideo->sisvga_engine == SIS_300_VGA) {
	           if(ivideo->vbflags & (VB_LVDS | VB_30xBDH)) {
		       int tmp;
		       inSISIDXREG(SISCR,0x30,tmp);
		       if(tmp & 0x20) {
		          /* Currently on LCD? If yes, read current pdc */
		          inSISIDXREG(SISPART1,0x13,ivideo->detectedpdc);
			  ivideo->detectedpdc &= 0x3c;
			  if(ivideo->SiS_Pr.PDC == -1) {
			     /* Let option override detection */
			     ivideo->SiS_Pr.PDC = ivideo->detectedpdc;
			  }
			  printk(KERN_INFO "sisfb: Detected LCD PDC 0x%02x\n",
  			         ivideo->detectedpdc);
		       }
		       if((ivideo->SiS_Pr.PDC != -1) && (ivideo->SiS_Pr.PDC != ivideo->detectedpdc)) {
		          printk(KERN_INFO "sisfb: Using LCD PDC 0x%02x\n",
				 ivideo->SiS_Pr.PDC);
		       }
	           }
		}
#endif

#ifdef CONFIG_FB_SIS_315
		if(ivideo->sisvga_engine == SIS_315_VGA) {

		   /* Try to find about LCDA */
		   if(ivideo->vbflags & (VB_301C | VB_302B | VB_301LV | VB_302LV | VB_302ELV)) {
		      int tmp;
		      inSISIDXREG(SISPART1,0x13,tmp);
		      if(tmp & 0x04) {
		         ivideo->SiS_Pr.SiS_UseLCDA = TRUE;
		         ivideo->detectedlcda = 0x03;
		      }
	           }

		   /* Save PDC */
		   if(ivideo->vbflags & (VB_301LV | VB_302LV | VB_302ELV)) {
		      int tmp;
		      inSISIDXREG(SISCR,0x30,tmp);
		      if((tmp & 0x20) || (ivideo->detectedlcda != 0xff)) {
		         /* Currently on LCD? If yes, read current pdc */
			 u8 pdc;
		         inSISIDXREG(SISPART1,0x2D,pdc);
			 ivideo->detectedpdc  = (pdc & 0x0f) << 1;
			 ivideo->detectedpdca = (pdc & 0xf0) >> 3;
			 inSISIDXREG(SISPART1,0x35,pdc);
			 ivideo->detectedpdc |= ((pdc >> 7) & 0x01);
			 inSISIDXREG(SISPART1,0x20,pdc);
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
		      if(ivideo->vbflags & (VB_302LV | VB_302ELV)) {
		         inSISIDXREG(SISPART4,0x30,ivideo->SiS_Pr.EMI_30);
			 inSISIDXREG(SISPART4,0x31,ivideo->SiS_Pr.EMI_31);
			 inSISIDXREG(SISPART4,0x32,ivideo->SiS_Pr.EMI_32);
			 inSISIDXREG(SISPART4,0x33,ivideo->SiS_Pr.EMI_33);
			 ivideo->SiS_Pr.HaveEMI = TRUE;
			 if((tmp & 0x20) || (ivideo->detectedlcda != 0xff)) {
			  	ivideo->SiS_Pr.HaveEMILCD = TRUE;
			 }
		      }
		   }

		   /* Let user override detected PDCs (all bridges) */
		   if(ivideo->vbflags & (VB_301B | VB_301C | VB_301LV | VB_302LV | VB_302ELV)) {
		      if((ivideo->SiS_Pr.PDC != -1) && (ivideo->SiS_Pr.PDC != ivideo->detectedpdc)) {
		         printk(KERN_INFO "sisfb: Using LCD PDC 0x%02x (for LCD=CRT2)\n",
				 ivideo->SiS_Pr.PDC);
		      }
		      if((ivideo->SiS_Pr.PDCA != -1) && (ivideo->SiS_Pr.PDCA != ivideo->detectedpdca)) {
		         printk(KERN_INFO "sisfb: Using LCD PDC1 0x%02x (for LCD=CRT1)\n",
				 ivideo->SiS_Pr.PDCA);
		      }
		   }

		}
#endif

		if(!ivideo->sisfb_crt1off) {
		   	sisfb_handle_ddc(ivideo, &ivideo->sisfb_thismonitor, 0);
		} else {
		   	if((ivideo->vbflags & (VB_301|VB_301B|VB_301C|VB_302B)) &&
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
			sisfb_search_refresh_rate(ivideo, ivideo->refresh_rate, ivideo->sisfb_mode_idx);
		}

		if(ivideo->rate_idx == 0) {
			ivideo->rate_idx = sisbios_mode[ivideo->sisfb_mode_idx].rate_idx;
			ivideo->refresh_rate = 60;
		}

		if(ivideo->sisfb_thismonitor.datavalid) {
			if(!sisfb_verify_rate(ivideo, &ivideo->sisfb_thismonitor, ivideo->sisfb_mode_idx,
			                      ivideo->rate_idx, ivideo->refresh_rate)) {
				printk(KERN_INFO "sisfb: WARNING: Refresh rate exceeds monitor specs!\n");
			}
		}

		ivideo->video_bpp = sisbios_mode[ivideo->sisfb_mode_idx].bpp;
		ivideo->video_width = sisbios_mode[ivideo->sisfb_mode_idx].xres;
		ivideo->video_height = sisbios_mode[ivideo->sisfb_mode_idx].yres;

		sisfb_set_vparms(ivideo);
		
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	

		/* ---------------- For 2.4: Now switch the mode ------------------ */		
		
		printk(KERN_INFO "sisfb: Mode is %dx%dx%d (%dHz)\n",
	       		ivideo->video_width, ivideo->video_height, ivideo->video_bpp,
			ivideo->refresh_rate);

		sisfb_pre_setmode(ivideo);

		if(SiSSetMode(&ivideo->SiS_Pr, &ivideo->sishw_ext, ivideo->mode_no) == 0) {
			printk(KERN_ERR "sisfb: Fatal error: Setting mode[0x%x] failed\n",
									ivideo->mode_no);
			iounmap(ivideo->video_vbase);
			iounmap(ivideo->mmio_vbase);
			release_mem_region(ivideo->video_base, ivideo->video_size);
			release_mem_region(ivideo->mmio_base, ivideo->mmio_size);
			if(ivideo->bios_abase) vfree(ivideo->bios_abase);
			pci_set_drvdata(pdev, NULL);
			kfree(sis_fb_info);
			return -EINVAL;
		}

		outSISIDXREG(SISSR, IND_SIS_PASSWORD, SIS_PASSWORD);

		sisfb_post_setmode(ivideo);

		/* Maximize regardless of sisfb_max at startup */
		ivideo->default_var.yres_virtual = 32767;

		/* Force reset of x virtual in crtc_to_var */
		ivideo->default_var.xres_virtual = 0;

		sisfb_crtc_to_var(ivideo, &ivideo->default_var);

		sisfb_calc_pitch(ivideo, &ivideo->default_var);
		sisfb_set_pitch(ivideo);

		ivideo->accel = 0;
		if(ivideo->sisfb_accel) {
		   ivideo->accel = -1;
		   ivideo->default_var.accel_flags |= FB_ACCELF_TEXT;
		}
		sisfb_initaccel(ivideo);
		
		sis_fb_info->node  = -1;
		sis_fb_info->flags = FBINFO_FLAG_DEFAULT;
		sis_fb_info->fbops = &sisfb_ops;
		sis_fb_info->disp  = &ivideo->sis_disp;
		sis_fb_info->blank = &sisfb_blank;
		sis_fb_info->switch_con = &sisfb_switch;
		sis_fb_info->updatevar  = &sisfb_update_var;
		sis_fb_info->changevar  = NULL;
		strcpy(sis_fb_info->fontname, sisfb_fontname);

		sisfb_set_disp(-1, &ivideo->default_var, sis_fb_info);

#else		/* --------- For 2.6: Setup a somewhat sane default var ------------ */

		printk(KERN_INFO "sisfb: Default mode is %dx%dx%d (%dHz)\n",
	       		ivideo->video_width, ivideo->video_height, ivideo->video_bpp,
			ivideo->refresh_rate);

		ivideo->default_var.xres = ivideo->default_var.xres_virtual = ivideo->video_width;
		ivideo->default_var.yres = ivideo->default_var.yres_virtual = ivideo->video_height;
		ivideo->default_var.bits_per_pixel = ivideo->video_bpp;

		sisfb_bpp_to_var(ivideo, &ivideo->default_var);
		
		ivideo->default_var.pixclock = (u32) (1000000000 /
				sisfb_mode_rate_to_dclock(&ivideo->SiS_Pr, &ivideo->sishw_ext,
						ivideo->mode_no, ivideo->rate_idx));
						
		if(sisfb_mode_rate_to_ddata(&ivideo->SiS_Pr, &ivideo->sishw_ext,
			 	ivideo->mode_no, ivideo->rate_idx, &ivideo->default_var)) {
		   if((ivideo->default_var.vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		      ivideo->default_var.pixclock <<= 1;
	   	   }
	        }

		if(ivideo->sisfb_ypan) {
		   /* Maximize regardless of sisfb_max at startup */
	    	   ivideo->default_var.yres_virtual = sisfb_calc_maxyres(ivideo, &ivideo->default_var);
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
		sis_fb_info->screen_base = ivideo->video_vbase;
		sis_fb_info->fbops = &sisfb_ops;

		sisfb_get_fix(&sis_fb_info->fix, -1, sis_fb_info);
		sis_fb_info->pseudo_palette = ivideo->pseudo_palette;
		
		fb_alloc_cmap(&sis_fb_info->cmap, 256 , 0);
#endif		/* 2.6 */

		printk(KERN_DEBUG "sisfb: Initial vbflags 0x%lx\n", (unsigned long)ivideo->vbflags);

#ifdef CONFIG_MTRR
		ivideo->mtrr = mtrr_add(ivideo->video_base, ivideo->video_size,
					MTRR_TYPE_WRCOMB, 1);
		if(!ivideo->mtrr) {
			printk(KERN_DEBUG "sisfb: Failed to add MTRRs\n");
		}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
		vc_resize_con(1, 1, 0);
#endif

		if(register_framebuffer(sis_fb_info) < 0) {
			printk(KERN_ERR "sisfb: Fatal error: Failed to register framebuffer\n");
			iounmap(ivideo->video_vbase);
			iounmap(ivideo->mmio_vbase);
			release_mem_region(ivideo->video_base, ivideo->video_size);
			release_mem_region(ivideo->mmio_base, ivideo->mmio_size);
			if(ivideo->bios_abase) vfree(ivideo->bios_abase);
			pci_set_drvdata(pdev, NULL);
			kfree(sis_fb_info);
			return -EINVAL;
		}

		ivideo->registered = 1;

		/* Enlist us */
		ivideo->next = card_list;
		card_list = ivideo;

		printk(KERN_INFO "sisfb: 2D acceleration is %s, y-panning %s\n",
		     ivideo->sisfb_accel ? "enabled" : "disabled",
		     ivideo->sisfb_ypan  ?
		     	(ivideo->sisfb_max ? "enabled (auto-max)" : "enabled (no auto-max)") : "disabled");


		printk(KERN_INFO "fb%d: %s frame buffer device, Version %d.%d.%d\n",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	       		GET_FB_IDX(sis_fb_info->node),
#else
	       		sis_fb_info->node,
#endif
			ivideo->myid, VER_MAJOR, VER_MINOR, VER_LEVEL);

		printk(KERN_INFO "sisfb: (C) 2001-2004 Thomas Winischhofer.\n");

	}	/* if mode = "none" */

	return 0;
}

/*****************************************************/
/*                PCI DEVICE HANDLING                */
/*****************************************************/

static void __devexit sisfb_remove(struct pci_dev *pdev)
{
	struct sis_video_info *ivideo = pci_get_drvdata(pdev);
	struct fb_info        *sis_fb_info = ivideo->memyselfandi;
	int                   registered = ivideo->registered;

	/* Unmap */
	iounmap(ivideo->video_vbase);
	iounmap(ivideo->mmio_vbase);
	vfree(ivideo->bios_abase);

	/* Release mem regions */
	release_mem_region(ivideo->video_base, ivideo->video_size);
	release_mem_region(ivideo->mmio_base, ivideo->mmio_size);

#ifdef CONFIG_MTRR
	/* Release MTRR region */
	if(ivideo->mtrr) {
		mtrr_del(ivideo->mtrr, ivideo->video_base, ivideo->video_size);
	}
#endif

	/* Unregister the framebuffer */
	if(ivideo->registered) {
		unregister_framebuffer(sis_fb_info);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,3))
		framebuffer_release(sis_fb_info);
#else
		kfree(sis_fb_info);
#endif
	}

	pci_set_drvdata(pdev, NULL);

	/* TODO: Restore the initial mode
	 * This sounds easy but is as good as impossible
	 * on many machines with SiS chip and video bridge
	 * since text modes are always set up differently
	 * from machine to machine. Depends on the type
	 * of integration between chipset and bridge.
	 */
	if(registered) {
	   printk(KERN_INFO "sisfb: Restoring of text mode not supported yet\n");
	}
};

static struct pci_driver sisfb_driver = {
	.name		= "sisfb",
	.id_table 	= sisfb_pci_table,
	.probe 		= sisfb_probe,
	.remove 	= __devexit_p(sisfb_remove)
};

SISINITSTATIC int __init sisfb_init(void)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
#ifndef MODULE
	char *options = NULL;

	if(fb_get_options("sisfb", &options))
		return -ENODEV;
	sisfb_setup(options);
#endif
#endif
	return(pci_register_driver(&sisfb_driver));
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
#ifndef MODULE
module_init(sisfb_init);
#endif
#endif

/*****************************************************/
/*                      MODULE                       */
/*****************************************************/

#ifdef MODULE

static char         *mode = NULL;
static int          vesa = -1;
static unsigned int rate = 0;
static unsigned int crt1off = 1;
static unsigned int mem = 0;
static char         *forcecrt2type = NULL;
static int          forcecrt1 = -1;
static int          pdc = -1;
static int          pdc1 = -1;
static int          noaccel = -1;
static int          noypan  = -1;
static int	    nomax = -1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int          inverse = 0;
#endif
static int          userom = -1;
static int          useoem = -1;
static char         *tvstandard = NULL;
static int	    nocrt2rate = 0;
static int          scalelcd = -1;
static char	    *specialtiming = NULL;
static int	    lvdshl = -1;
static int	    tvxposoffset = 0, tvyposoffset = 0;
static int	    filter = -1;
#if !defined(__i386__) && !defined(__x86_64__)
static int	    resetcard = 0;
static int	    videoram = 0;
#endif

MODULE_DESCRIPTION("SiS 300/540/630/730/315/550/65x/661/74x/330/760 framebuffer device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Winischhofer <thomas@winischhofer.net>, Others");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MODULE_PARM(mem, "i");
MODULE_PARM(noaccel, "i");
MODULE_PARM(noypan, "i");
MODULE_PARM(nomax, "i");
MODULE_PARM(userom, "i");
MODULE_PARM(useoem, "i");
MODULE_PARM(mode, "s");
MODULE_PARM(vesa, "i");
MODULE_PARM(rate, "i");
MODULE_PARM(forcecrt1, "i");
MODULE_PARM(forcecrt2type, "s");
MODULE_PARM(scalelcd, "i");
MODULE_PARM(pdc, "i");
MODULE_PARM(pdc1, "i");
MODULE_PARM(specialtiming, "s");
MODULE_PARM(lvdshl, "i");
MODULE_PARM(tvstandard, "s");
MODULE_PARM(tvxposoffset, "i");
MODULE_PARM(tvyposoffset, "i");
MODULE_PARM(filter, "i");
MODULE_PARM(nocrt2rate, "i");
MODULE_PARM(inverse, "i");
#if !defined(__i386__) && !defined(__x86_64__)
MODULE_PARM(resetcard, "i");
MODULE_PARM(videoram, "i");
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
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
module_param(filter, int, 0);
module_param(nocrt2rate, int, 0);
#if !defined(__i386__) && !defined(__x86_64__)
module_param(resetcard, int, 0);
module_param(videoram, int, 0);
#endif
#endif

MODULE_PARM_DESC(mem,
	"\nDetermines the beginning of the video memory heap in KB. This heap is used\n"
	  "for video RAM management for eg. DRM/DRI. On 300 series, the default depends\n"
	  "on the amount of video RAM available. If 8MB of video RAM or less is available,\n"
	  "the heap starts at 4096KB, if between 8 and 16MB are available at 8192KB,\n"
	  "otherwise at 12288KB. On 315 and Xabre series, the heap size is 32KB by default.\n"
	  "The value is to be specified without 'KB' and must match the MaxXFBMem setting\n"
	  "for XFree86 4.x/X.org 6.7 and later.\n");

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MODULE_PARM_DESC(mode,
        "\nSelects the desired display mode in the format [X]x[Y]x[Depth], eg.\n"
          "1024x768x16. Other formats supported include XxY-Depth and\n"
 	  "XxY-Depth@Rate. If the parameter is only one (decimal or hexadecimal)\n"
	  "number, it will be interpreted as a VESA mode number. (default: none if\n"
	  "sisfb is a module; this leaves the console untouched and the driver will\n"
	  "only do the video memory management for eg. DRM/DRI; 800x600x8 if sisfb\n"
	  "is in the kernel)\n");
MODULE_PARM_DESC(vesa,
        "\nSelects the desired display mode by VESA defined mode number, eg. 0x117\n"
          "(default: 0x0000 if sisfb is a module; this leaves the console untouched\n"
	  "and the driver will only do the video memory management for eg. DRM/DRI;\n"
	  "0x0103 if sisfb is in the kernel)\n");
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
MODULE_PARM_DESC(mode,
       "\nSelects the desired default display mode in the format XxYxDepth,\n"
         "eg. 1024x768x16. Other formats supported include XxY-Depth and\n"
	 "XxY-Depth@Rate. If the parameter is only one (decimal or hexadecimal)\n"
	 "number, it will be interpreted as a VESA mode number. (default: 800x600x8)\n");

MODULE_PARM_DESC(vesa,
       "\nSelects the desired default display mode by VESA defined mode number, eg.\n"
         "0x117 (default: 0x0103)\n");
#endif

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
	  "on a 300 series chipset; 6 on a 315 series chipset. If the problem persists,\n"
	  "try other values (on 300 series: between 4 and 60 in steps of 4; on 315 series:\n"
	  "any value from 0 to 31). (default: autodetected, if LCD is active during start)\n");

#ifdef CONFIG_FB_SIS_315
MODULE_PARM_DESC(pdc1,
        "\nThis is same as pdc, but for LCD-via CRT1. Hence, this is for the 315/330\n"
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

MODULE_PARM_DESC(filter,
	"\nSelects TV flicker filter type (only for systems with a SiS301 video bridge).\n"
	  "(Possible values 0-7, default: [no filter])\n");

MODULE_PARM_DESC(nocrt2rate,
	"\nSetting this to 1 will force the driver to use the default refresh rate for\n"
	  "CRT2 if CRT2 type is VGA. (default: 0, use same rate as CRT1)\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
MODULE_PARM_DESC(inverse,
        "\nSetting this to anything but 0 should invert the display colors, but this\n"
	  "does not seem to work. (default: 0)\n");
#endif

#if !defined(__i386__) && !defined(__x86_64__)
#ifdef CONFIG_FB_SIS_300
MODULE_PARM_DESC(resetcard,
	"\nSet this to 1 in order to reset (POST) the card on non-x86 machines where\n"
	  "the BIOS did not POST the card (only supported for SiS 300/305 currently).\n"
	  "Default: 0\n");

MODULE_PARM_DESC(videoram,
	"\nSet this to the amount of video RAM (in kilobyte) the card has. Required on\n"
	  "some non-x86 architectures where the memory auto detection fails. Only\n"
	  "relevant if resetcard is set, too. Default: [auto-detect]\n");
#endif
#endif

static int __devinit sisfb_init_module(void)
{
	sisfb_setdefaultparms();

	if(rate) sisfb_parm_rate = rate;

	if((scalelcd == 0) || (scalelcd == 1)) {
	   sisfb_scalelcd = scalelcd ^ 1;
	}

	/* Need to check crt2 type first for fstn/dstn */

	if(forcecrt2type)
		sisfb_search_crt2type(forcecrt2type);

	if(tvstandard)
		sisfb_search_tvstd(tvstandard);

	if(mode)
		sisfb_search_mode(mode, FALSE);
	else if(vesa != -1)
		sisfb_search_vesamode(vesa, FALSE);

	sisfb_crt1off = (crt1off == 0) ? 1 : 0;

	sisfb_forcecrt1 = forcecrt1;
	if(forcecrt1 == 1)      sisfb_crt1off = 0;
	else if(forcecrt1 == 0) sisfb_crt1off = 1;

	if(noaccel == 1)      sisfb_accel = 0;
	else if(noaccel == 0) sisfb_accel = 1;

	if(noypan == 1)       sisfb_ypan = 0;
	else if(noypan == 0)  sisfb_ypan = 1;

	if(nomax == 1)        sisfb_max = 0;
	else if(nomax == 0)   sisfb_max = 1;
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	if(inverse)           sisfb_inverse = 1;
#endif

	if(mem)		      sisfb_parm_mem = mem;

	if(userom != -1)      sisfb_userom = userom;
	if(useoem != -1)      sisfb_useoem = useoem;

        if(pdc != -1)  sisfb_pdc  = (pdc  & 0x7f);
	if(pdc1 != -1) sisfb_pdca = (pdc1 & 0x1f);

	sisfb_nocrt2rate = nocrt2rate;

	if(specialtiming)
		sisfb_search_specialtiming(specialtiming);

	if((lvdshl >= 0) && (lvdshl <= 3))  sisfb_lvdshl = lvdshl;

	if(filter != -1) sisfb_filter = filter;

	sisfb_tvxposoffset = tvxposoffset;
	sisfb_tvyposoffset = tvyposoffset;

#if !defined(__i386__) && !defined(__x86_64__)
 	sisfb_resetcard = (resetcard) ? 1 : 0;
	if(videoram)    sisfb_videoram = videoram;
#endif

        return(sisfb_init());
}

static void __exit sisfb_remove_module(void)
{
	pci_unregister_driver(&sisfb_driver);
	printk(KERN_DEBUG "sisfb: Module unloaded\n");
}

module_init(sisfb_init_module);
module_exit(sisfb_remove_module);

#endif 	   /*  /MODULE  */

EXPORT_SYMBOL(sis_malloc);
EXPORT_SYMBOL(sis_free);


