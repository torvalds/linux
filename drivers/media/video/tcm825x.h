/*
 * drivers/media/video/tcm825x.h
 *
 * Register definitions for the TCM825X CameraChip.
 *
 * Author: David Cohen (david.cohen@indt.org.br)
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * This file was based on ov9640.h from MontaVista
 */

#ifndef TCM825X_H
#define TCM825X_H

#include <linux/videodev2.h>

#include <media/v4l2-int-device.h>

#define TCM825X_NAME "tcm825x"

#define TCM825X_MASK(x)  x & 0x00ff
#define TCM825X_ADDR(x) (x & 0xff00) >> 8

/* The TCM825X I2C sensor chip has a fixed slave address of 0x3d. */
#define TCM825X_I2C_ADDR	0x3d

/*
 * define register offsets for the TCM825X sensor chip
 * OFFSET(8 bits) + MASK(8 bits)
 * MASK bit 4 and 3 are used when the register uses more than one address
 */
#define TCM825X_FPS		0x0280
#define TCM825X_ACF		0x0240
#define TCM825X_DOUTBUF		0x020C
#define TCM825X_DCLKP		0x0202
#define TCM825X_ACFDET		0x0201
#define TCM825X_DOUTSW		0x0380
#define TCM825X_DATAHZ		0x0340
#define TCM825X_PICSIZ		0x033c
#define TCM825X_PICFMT		0x0302
#define TCM825X_V_INV		0x0480
#define TCM825X_H_INV		0x0440
#define TCM825X_ESRLSW		0x0430
#define TCM825X_V_LENGTH	0x040F
#define TCM825X_ALCSW		0x0580
#define TCM825X_ESRLIM		0x0560
#define TCM825X_ESRSPD_U        0x051F
#define TCM825X_ESRSPD_L        0x06FF
#define TCM825X_AG		0x07FF
#define TCM825X_ESRSPD2         0x06FF
#define TCM825X_ALCMODE         0x0830
#define TCM825X_ALCH            0x080F
#define TCM825X_ALCL            0x09FF
#define TCM825X_AWBSW           0x0A80
#define TCM825X_MRG             0x0BFF
#define TCM825X_MBG             0x0CFF
#define TCM825X_GAMSW           0x0D80
#define TCM825X_HDTG            0x0EFF
#define TCM825X_VDTG            0x0FFF
#define TCM825X_HDTCORE         0x10F0
#define TCM825X_VDTCORE         0x100F
#define TCM825X_CONT            0x11FF
#define TCM825X_BRIGHT          0x12FF
#define TCM825X_VHUE            0x137F
#define TCM825X_UHUE            0x147F
#define TCM825X_VGAIN           0x153F
#define TCM825X_UGAIN           0x163F
#define TCM825X_UVCORE          0x170F
#define TCM825X_SATU            0x187F
#define TCM825X_MHMODE          0x1980
#define TCM825X_MHLPFSEL        0x1940
#define TCM825X_YMODE           0x1930
#define TCM825X_MIXHG           0x1907
#define TCM825X_LENS            0x1A3F
#define TCM825X_AGLIM           0x1BE0
#define TCM825X_LENSRPOL        0x1B10
#define TCM825X_LENSRGAIN       0x1B0F
#define TCM825X_ES100S          0x1CFF
#define TCM825X_ES120S          0x1DFF
#define TCM825X_DMASK           0x1EC0
#define TCM825X_CODESW          0x1E20
#define TCM825X_CODESEL         0x1E10
#define TCM825X_TESPIC          0x1E04
#define TCM825X_PICSEL          0x1E03
#define TCM825X_HNUM            0x20FF
#define TCM825X_VOUTPH          0x287F
#define TCM825X_ESROUT          0x327F
#define TCM825X_ESROUT2         0x33FF
#define TCM825X_AGOUT           0x34FF
#define TCM825X_DGOUT           0x353F
#define TCM825X_AGSLOW1         0x39C0
#define TCM825X_FLLSMODE        0x3930
#define TCM825X_FLLSLIM         0x390F
#define TCM825X_DETSEL          0x3AF0
#define TCM825X_ACDETNC         0x3A0F
#define TCM825X_AGSLOW2         0x3BC0
#define TCM825X_DG              0x3B3F
#define TCM825X_REJHLEV         0x3CFF
#define TCM825X_ALCLOCK         0x3D80
#define TCM825X_FPSLNKSW        0x3D40
#define TCM825X_ALCSPD          0x3D30
#define TCM825X_REJH            0x3D03
#define TCM825X_SHESRSW         0x3E80
#define TCM825X_ESLIMSEL        0x3E40
#define TCM825X_SHESRSPD        0x3E30
#define TCM825X_ELSTEP          0x3E0C
#define TCM825X_ELSTART         0x3E03
#define TCM825X_AGMIN           0x3FFF
#define TCM825X_PREGRG          0x423F
#define TCM825X_PREGBG          0x433F
#define TCM825X_PRERG           0x443F
#define TCM825X_PREBG           0x453F
#define TCM825X_MSKBR           0x477F
#define TCM825X_MSKGR           0x487F
#define TCM825X_MSKRB           0x497F
#define TCM825X_MSKGB           0x4A7F
#define TCM825X_MSKRG           0x4B7F
#define TCM825X_MSKBG           0x4C7F
#define TCM825X_HDTCSW          0x4D80
#define TCM825X_VDTCSW          0x4D40
#define TCM825X_DTCYL           0x4D3F
#define TCM825X_HDTPSW          0x4E80
#define TCM825X_VDTPSW          0x4E40
#define TCM825X_DTCGAIN         0x4E3F
#define TCM825X_DTLLIMSW        0x4F10
#define TCM825X_DTLYLIM         0x4F0F
#define TCM825X_YLCUTLMSK       0x5080
#define TCM825X_YLCUTL          0x503F
#define TCM825X_YLCUTHMSK       0x5180
#define TCM825X_YLCUTH          0x513F
#define TCM825X_UVSKNC          0x527F
#define TCM825X_UVLJ            0x537F
#define TCM825X_WBGMIN          0x54FF
#define TCM825X_WBGMAX          0x55FF
#define TCM825X_WBSPDUP         0x5603
#define TCM825X_ALLAREA         0x5820
#define TCM825X_WBLOCK          0x5810
#define TCM825X_WB2SP           0x580F
#define TCM825X_KIZUSW          0x5920
#define TCM825X_PBRSW           0x5910
#define TCM825X_ABCSW           0x5903
#define TCM825X_PBDLV           0x5AFF
#define TCM825X_PBC1LV          0x5BFF

#define TCM825X_NUM_REGS	(TCM825X_ADDR(TCM825X_PBC1LV) + 1)

#define TCM825X_BYTES_PER_PIXEL 2

#define TCM825X_REG_TERM 0xff		/* terminating list entry for reg */
#define TCM825X_VAL_TERM 0xff		/* terminating list entry for val */

/* define a structure for tcm825x register initialization values */
struct tcm825x_reg {
	u8 val;
	u16 reg;
};

enum image_size { subQCIF = 0, QQVGA, QCIF, QVGA, CIF, VGA };
enum pixel_format { YUV422 = 0, RGB565 };
#define NUM_IMAGE_SIZES 6
#define NUM_PIXEL_FORMATS 2

#define TCM825X_XCLK_MIN	11900000
#define TCM825X_XCLK_MAX	25000000

struct capture_size {
	unsigned long width;
	unsigned long height;
};

struct tcm825x_platform_data {
	/* Is the sensor usable? Doesn't yet mean it's there, but you
	 * can try! */
	int (*is_okay)(void);
	/* Set power state, zero is off, non-zero is on. */
	int (*power_set)(int power);
	/* Default registers written after power-on or reset. */
	const struct tcm825x_reg *(*default_regs)(void);
	int (*needs_reset)(struct v4l2_int_device *s, void *buf,
			   struct v4l2_pix_format *fmt);
	int (*ifparm)(struct v4l2_ifparm *p);
};

/* Array of image sizes supported by TCM825X.  These must be ordered from
 * smallest image size to largest.
 */
const static struct capture_size tcm825x_sizes[] = {
	{ 128,  96 }, /* subQCIF */
	{ 160, 120 }, /* QQVGA */
	{ 176, 144 }, /* QCIF */
	{ 320, 240 }, /* QVGA */
	{ 352, 288 }, /* CIF */
	{ 640, 480 }, /* VGA */
};

#endif /* ifndef TCM825X_H */
