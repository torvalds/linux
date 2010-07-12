# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@users.sf.net>
# Rewritten to use lists instead of if-statements.

# Each configuration option enables a list of files.

obj-$(CONFIG_VGASTATE)            += vgastate.o
obj-y                             += fb_notify.o
obj-$(CONFIG_FB)                  += fb.o
fb-y                              := fbmem.o fbmon.o fbcmap.o fbsysfs.o \
                                     modedb.o fbcvt.o
fb-objs                           := $(fb-y)

obj-$(CONFIG_VT)		  += console/
obj-$(CONFIG_LOGO)		  += logo/
obj-y				  += backlight/ display/

obj-$(CONFIG_FB_CFB_FILLRECT)  += cfbfillrect.o
obj-$(CONFIG_FB_CFB_COPYAREA)  += cfbcopyarea.o
obj-$(CONFIG_FB_CFB_IMAGEBLIT) += cfbimgblt.o
obj-$(CONFIG_FB_SYS_FILLRECT)  += sysfillrect.o
obj-$(CONFIG_FB_SYS_COPYAREA)  += syscopyarea.o
obj-$(CONFIG_FB_SYS_IMAGEBLIT) += sysimgblt.o
obj-$(CONFIG_FB_SYS_FOPS)      += fb_sys_fops.o
obj-$(CONFIG_FB_SVGALIB)       += svgalib.o
obj-$(CONFIG_FB_MACMODES)      += macmodes.o
obj-$(CONFIG_FB_DDC)           += fb_ddc.o
obj-$(CONFIG_FB_DEFERRED_IO)   += fb_defio.o

# Hardware specific drivers go first
obj-$(CONFIG_FB_AMIGA)            += amifb.o c2p_planar.o
obj-$(CONFIG_FB_ARC)              += arcfb.o
obj-$(CONFIG_FB_CLPS711X)         += clps711xfb.o
obj-$(CONFIG_FB_CYBER2000)        += cyber2000fb.o
obj-$(CONFIG_FB_PM2)              += pm2fb.o
obj-$(CONFIG_FB_PM3)		  += pm3fb.o

obj-$(CONFIG_FB_MATROX)		  += matrox/
obj-$(CONFIG_FB_RIVA)		  += riva/
obj-$(CONFIG_FB_NVIDIA)		  += nvidia/
obj-$(CONFIG_FB_ATY)		  += aty/ macmodes.o
obj-$(CONFIG_FB_ATY128)		  += aty/ macmodes.o
obj-$(CONFIG_FB_RADEON)		  += aty/
obj-$(CONFIG_FB_SIS)		  += sis/
obj-$(CONFIG_FB_VIA)		  += via/
obj-$(CONFIG_FB_KYRO)             += kyro/
obj-$(CONFIG_FB_SAVAGE)		  += savage/
obj-$(CONFIG_FB_GEODE)		  += geode/
obj-$(CONFIG_FB_MBX)		  += mbx/
obj-$(CONFIG_FB_NEOMAGIC)         += neofb.o
obj-$(CONFIG_FB_3DFX)             += tdfxfb.o
obj-$(CONFIG_FB_CONTROL)          += controlfb.o
obj-$(CONFIG_FB_PLATINUM)         += platinumfb.o
obj-$(CONFIG_FB_VALKYRIE)         += valkyriefb.o
obj-$(CONFIG_FB_CT65550)          += chipsfb.o
obj-$(CONFIG_FB_IMSTT)            += imsttfb.o
obj-$(CONFIG_FB_FM2)              += fm2fb.o
obj-$(CONFIG_FB_VT8623)           += vt8623fb.o
obj-$(CONFIG_FB_TRIDENT)          += tridentfb.o
obj-$(CONFIG_FB_LE80578)          += vermilion/
obj-$(CONFIG_FB_S3)               += s3fb.o
obj-$(CONFIG_FB_ARK)              += arkfb.o
obj-$(CONFIG_FB_STI)              += stifb.o
obj-$(CONFIG_FB_FFB)              += ffb.o sbuslib.o
obj-$(CONFIG_FB_CG6)              += cg6.o sbuslib.o
obj-$(CONFIG_FB_CG3)              += cg3.o sbuslib.o
obj-$(CONFIG_FB_BW2)              += bw2.o sbuslib.o
obj-$(CONFIG_FB_CG14)             += cg14.o sbuslib.o
obj-$(CONFIG_FB_P9100)            += p9100.o sbuslib.o
obj-$(CONFIG_FB_TCX)              += tcx.o sbuslib.o
obj-$(CONFIG_FB_LEO)              += leo.o sbuslib.o
obj-$(CONFIG_FB_SGIVW)            += sgivwfb.o
obj-$(CONFIG_FB_ACORN)            += acornfb.o
obj-$(CONFIG_FB_ATARI)            += atafb.o c2p_iplan2.o atafb_mfb.o \
                                     atafb_iplan2p2.o atafb_iplan2p4.o atafb_iplan2p8.o
obj-$(CONFIG_FB_MAC)              += macfb.o
obj-$(CONFIG_FB_HECUBA)           += hecubafb.o
obj-$(CONFIG_FB_N411)             += n411.o
obj-$(CONFIG_FB_HGA)              += hgafb.o
obj-$(CONFIG_FB_XVR500)           += sunxvr500.o
obj-$(CONFIG_FB_XVR2500)          += sunxvr2500.o
obj-$(CONFIG_FB_XVR1000)          += sunxvr1000.o
obj-$(CONFIG_FB_IGA)              += igafb.o
obj-$(CONFIG_FB_APOLLO)           += dnfb.o
obj-$(CONFIG_FB_Q40)              += q40fb.o
obj-$(CONFIG_FB_TGA)              += tgafb.o
obj-$(CONFIG_FB_HP300)            += hpfb.o
obj-$(CONFIG_FB_G364)             += g364fb.o
obj-$(CONFIG_FB_EP93XX)		  += ep93xx-fb.o
obj-$(CONFIG_FB_SA1100)           += sa1100fb.o
obj-$(CONFIG_FB_HIT)              += hitfb.o
obj-$(CONFIG_FB_EPSON1355)	  += epson1355fb.o
obj-$(CONFIG_FB_ATMEL)		  += atmel_lcdfb.o
obj-$(CONFIG_FB_PVR2)             += pvr2fb.o
obj-$(CONFIG_FB_VOODOO1)          += sstfb.o
obj-$(CONFIG_FB_ARMCLCD)	  += amba-clcd.o
obj-$(CONFIG_FB_68328)            += 68328fb.o
obj-$(CONFIG_FB_GBE)              += gbefb.o
obj-$(CONFIG_FB_CIRRUS)		  += cirrusfb.o
obj-$(CONFIG_FB_ASILIANT)	  += asiliantfb.o
obj-$(CONFIG_FB_PXA)		  += pxafb.o
obj-$(CONFIG_FB_PXA168)		  += pxa168fb.o
obj-$(CONFIG_FB_W100)		  += w100fb.o
obj-$(CONFIG_FB_TMIO)		  += tmiofb.o
obj-$(CONFIG_FB_AU1100)		  += au1100fb.o
obj-$(CONFIG_FB_AU1200)		  += au1200fb.o
obj-$(CONFIG_FB_PMAG_AA)	  += pmag-aa-fb.o
obj-$(CONFIG_FB_PMAG_BA)	  += pmag-ba-fb.o
obj-$(CONFIG_FB_PMAGB_B)	  += pmagb-b-fb.o
obj-$(CONFIG_FB_MAXINE)		  += maxinefb.o
obj-$(CONFIG_FB_METRONOME)        += metronomefb.o
obj-$(CONFIG_FB_BROADSHEET)       += broadsheetfb.o
obj-$(CONFIG_FB_S1D13XXX)	  += s1d13xxxfb.o
obj-$(CONFIG_FB_SH7760)		  += sh7760fb.o
obj-$(CONFIG_FB_IMX)              += imxfb.o
obj-$(CONFIG_FB_S3C)		  += s3c-fb.o
obj-$(CONFIG_FB_S3C2410)	  += s3c2410fb.o
obj-$(CONFIG_FB_FSL_DIU)	  += fsl-diu-fb.o
obj-$(CONFIG_FB_COBALT)           += cobalt_lcdfb.o
obj-$(CONFIG_FB_PNX4008_DUM)	  += pnx4008/
obj-$(CONFIG_FB_PNX4008_DUM_RGB)  += pnx4008/
obj-$(CONFIG_FB_IBM_GXT4500)	  += gxt4500.o
obj-$(CONFIG_FB_PS3)		  += ps3fb.o
obj-$(CONFIG_FB_SM501)            += sm501fb.o
obj-$(CONFIG_FB_XILINX)           += xilinxfb.o
obj-$(CONFIG_SH_MIPI_DSI)	  += sh_mipi_dsi.o
obj-$(CONFIG_FB_SH_MOBILE_HDMI)	  += sh_mobile_hdmi.o
obj-$(CONFIG_FB_SH_MOBILE_LCDC)	  += sh_mobile_lcdcfb.o
obj-$(CONFIG_FB_OMAP)             += omap/
obj-y                             += omap2/
obj-$(CONFIG_XEN_FBDEV_FRONTEND)  += xen-fbfront.o
obj-$(CONFIG_FB_CARMINE)          += carminefb.o
obj-$(CONFIG_FB_MB862XX)	  += mb862xx/
obj-$(CONFIG_FB_MSM)              += msm/
obj-$(CONFIG_FB_NUC900)           += nuc900fb.o
obj-$(CONFIG_FB_JZ4740)		  += jz4740_fb.o
obj-y                             += tegra/

# Platform or fallback drivers go here
obj-$(CONFIG_FB_UVESA)            += uvesafb.o
obj-$(CONFIG_FB_VESA)             += vesafb.o
obj-$(CONFIG_FB_EFI)              += efifb.o
obj-$(CONFIG_FB_VGA16)            += vga16fb.o
obj-$(CONFIG_FB_OF)               += offb.o
obj-$(CONFIG_FB_BF54X_LQ043)	  += bf54x-lq043fb.o
obj-$(CONFIG_FB_BFIN_LQ035Q1)     += bfin-lq035q1-fb.o
obj-$(CONFIG_FB_BFIN_T350MCQB)	  += bfin-t350mcqb-fb.o
obj-$(CONFIG_FB_MX3)		  += mx3fb.o
obj-$(CONFIG_FB_DA8XX)		  += da8xx-fb.o

# the test framebuffer is last
obj-$(CONFIG_FB_VIRTUAL)          += vfb.o

#video output switch sysfs driver
obj-$(CONFIG_VIDEO_OUTPUT_CONTROL) += output.o
