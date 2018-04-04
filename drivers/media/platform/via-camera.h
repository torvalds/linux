/* SPDX-License-Identifier: GPL-2.0 */
/*
 * VIA Camera register definitions.
 */
#define VCR_INTCTRL	0x300	/* Capture interrupt control */
#define   VCR_IC_EAV	  0x0001   /* End of active video status */
#define	  VCR_IC_EVBI	  0x0002   /* End of VBI status */
#define   VCR_IC_FBOTFLD  0x0004   /* "flipping" Bottom field is active */
#define   VCR_IC_ACTBUF	  0x0018   /* Active video buffer  */
#define   VCR_IC_VSYNC	  0x0020   /* 0 = VB, 1 = active video */
#define   VCR_IC_BOTFLD	  0x0040   /* Bottom field is active */
#define   VCR_IC_FFULL	  0x0080   /* FIFO full */
#define   VCR_IC_INTEN	  0x0100   /* End of active video int. enable */
#define   VCR_IC_VBIINT	  0x0200   /* End of VBI int enable */
#define   VCR_IC_VBIBUF	  0x0400   /* Current VBI buffer */

#define VCR_TSC		0x308	/* Transport stream control */
#define VCR_TSC_ENABLE    0x000001   /* Transport stream input enable */
#define VCR_TSC_DROPERR   0x000002   /* Drop error packets */
#define VCR_TSC_METHOD    0x00000c   /* DMA method (non-functional) */
#define VCR_TSC_COUNT     0x07fff0   /* KByte or packet count */
#define VCR_TSC_CBMODE	  0x080000   /* Change buffer by byte count */
#define VCR_TSC_PSSIG	  0x100000   /* Packet starting signal disable */
#define VCR_TSC_BE	  0x200000   /* MSB first (serial mode) */
#define VCR_TSC_SERIAL	  0x400000   /* Serial input (0 = parallel) */

#define VCR_CAPINTC	0x310	/* Capture interface control */
#define   VCR_CI_ENABLE   0x00000001  /* Capture enable */
#define   VCR_CI_BSS	  0x00000002  /* WTF "bit stream selection" */
#define   VCR_CI_3BUFS	  0x00000004  /* 1 = 3 buffers, 0 = 2 buffers */
#define   VCR_CI_VIPEN	  0x00000008  /* VIP enable */
#define   VCR_CI_CCIR601_8  0		/* CCIR601 input stream, 8 bit */
#define   VCR_CI_CCIR656_8  0x00000010  /* ... CCIR656, 8 bit */
#define   VCR_CI_CCIR601_16 0x00000020  /* ... CCIR601, 16 bit */
#define   VCR_CI_CCIR656_16 0x00000030  /* ... CCIR656, 16 bit */
#define   VCR_CI_HDMODE   0x00000040  /* CCIR656-16 hdr decode mode; 1=16b */
#define   VCR_CI_BSWAP    0x00000080  /* Swap bytes (16-bit) */
#define   VCR_CI_YUYV	  0	      /* Byte order 0123 */
#define   VCR_CI_UYVY	  0x00000100  /* Byte order 1032 */
#define   VCR_CI_YVYU	  0x00000200  /* Byte order 0321 */
#define   VCR_CI_VYUY	  0x00000300  /* Byte order 3012 */
#define   VCR_CI_VIPTYPE  0x00000400  /* VIP type */
#define   VCR_CI_IFSEN    0x00000800  /* Input field signal enable */
#define   VCR_CI_DIODD	  0	      /* De-interlace odd, 30fps */
#define   VCR_CI_DIEVEN   0x00001000  /*    ...even field, 30fps */
#define   VCR_CI_DIBOTH   0x00002000  /*    ...both fields, 60fps */
#define   VCR_CI_DIBOTH30 0x00003000  /*    ...both fields, 30fps interlace */
#define   VCR_CI_CONVTYPE 0x00004000  /* 4:2:2 to 4:4:4; 1 = interpolate */
#define   VCR_CI_CFC	  0x00008000  /* Capture flipping control */
#define   VCR_CI_FILTER   0x00070000  /* Horiz filter mode select
					 000 = none
					 001 = 2 tap
					 010 = 3 tap
					 011 = 4 tap
					 100 = 5 tap */
#define   VCR_CI_CLKINV   0x00080000  /* Input CLK inverted */
#define   VCR_CI_VREFINV  0x00100000  /* VREF inverted */
#define   VCR_CI_HREFINV  0x00200000  /* HREF inverted */
#define   VCR_CI_FLDINV   0x00400000  /* Field inverted */
#define   VCR_CI_CLKPIN	  0x00800000  /* Capture clock pin */
#define   VCR_CI_THRESH   0x0f000000  /* Capture fifo threshold */
#define   VCR_CI_HRLE     0x10000000  /* Positive edge of HREF */
#define   VCR_CI_VRLE     0x20000000  /* Positive edge of VREF */
#define   VCR_CI_OFLDINV  0x40000000  /* Field output inverted */
#define   VCR_CI_CLKEN    0x80000000  /* Capture clock enable */

#define VCR_HORRANGE	0x314	/* Active video horizontal range */
#define VCR_VERTRANGE	0x318	/* Active video vertical range */
#define VCR_AVSCALE	0x31c	/* Active video scaling control */
#define   VCR_AVS_HEN	  0x00000800   /* Horizontal scale enable */
#define   VCR_AVS_VEN	  0x04000000   /* Vertical enable */
#define VCR_VBIHOR	0x320	/* VBI Data horizontal range */
#define VCR_VBIVERT	0x324	/* VBI data vertical range */
#define VCR_VBIBUF1	0x328	/* First VBI buffer */
#define VCR_VBISTRIDE	0x32c	/* VBI stride */
#define VCR_ANCDATACNT	0x330	/* Ancillary data count setting */
#define VCR_MAXDATA	0x334	/* Active data count of active video */
#define VCR_MAXVBI	0x338	/* Maximum data count of VBI */
#define VCR_CAPDATA	0x33c	/* Capture data count */
#define VCR_VBUF1	0x340	/* First video buffer */
#define VCR_VBUF2	0x344	/* Second video buffer */
#define VCR_VBUF3	0x348	/* Third video buffer */
#define VCR_VBUF_MASK	0x1ffffff0	/* Bits 28:4 */
#define VCR_VBIBUF2	0x34c	/* Second VBI buffer */
#define VCR_VSTRIDE	0x350	/* Stride of video + coring control */
#define   VCR_VS_STRIDE_SHIFT 4
#define   VCR_VS_STRIDE   0x00001ff0  /* Stride (8-byte units) */
#define   VCR_VS_CCD	  0x007f0000  /* Coring compare data */
#define   VCR_VS_COREEN   0x00800000  /* Coring enable */
#define VCR_TS0ERR	0x354	/* TS buffer 0 error indicator */
#define VCR_TS1ERR	0x358	/* TS buffer 0 error indicator */
#define VCR_TS2ERR	0x35c	/* TS buffer 0 error indicator */

/* Add 0x1000 for the second capture engine registers */
