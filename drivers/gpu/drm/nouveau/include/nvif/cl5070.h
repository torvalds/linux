/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CL5070_H__
#define __NVIF_CL5070_H__

#define NV50_DISP_MTHD                                                     0x00

struct nv50_disp_mthd_v0 {
	__u8  version;
#define NV50_DISP_SCANOUTPOS                                               0x00
	__u8  method;
	__u8  head;
	__u8  pad03[5];
};

struct nv50_disp_scanoutpos_v0 {
	__u8  version;
	__u8  pad01[7];
	__s64 time[2];
	__u16 vblanks;
	__u16 vblanke;
	__u16 vtotal;
	__u16 vline;
	__u16 hblanks;
	__u16 hblanke;
	__u16 htotal;
	__u16 hline;
};
#endif
