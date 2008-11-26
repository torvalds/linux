/*
 *	Video4Linux bw-qcam driver
 *
 *	Derived from code..
 */

/******************************************************************

Copyright (C) 1996 by Scott Laird

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL SCOTT LAIRD BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

/* One from column A... */
#define QC_NOTSET 0
#define QC_UNIDIR 1
#define QC_BIDIR  2
#define QC_SERIAL 3

/* ... and one from column B */
#define QC_ANY          0x00
#define QC_FORCE_UNIDIR 0x10
#define QC_FORCE_BIDIR  0x20
#define QC_FORCE_SERIAL 0x30
/* in the port_mode member */

#define QC_MODE_MASK    0x07
#define QC_FORCE_MASK   0x70

#define MAX_HEIGHT 243
#define MAX_WIDTH 336

/* Bit fields for status flags */
#define QC_PARAM_CHANGE	0x01 /* Camera status change has occurred */

struct qcam_device {
	struct video_device vdev;
	struct pardevice *pdev;
	struct parport *pport;
	struct mutex lock;
	int width, height;
	int bpp;
	int mode;
	int contrast, brightness, whitebal;
	int port_mode;
	int transfer_scale;
	int top, left;
	int status;
	unsigned int saved_bits;
	unsigned long in_use;
};
