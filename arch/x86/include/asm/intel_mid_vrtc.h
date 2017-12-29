/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_MID_VRTC_H
#define _INTEL_MID_VRTC_H

extern unsigned char vrtc_cmos_read(unsigned char reg);
extern void vrtc_cmos_write(unsigned char val, unsigned char reg);
extern void vrtc_get_time(struct timespec *now);
extern int vrtc_set_mmss(const struct timespec *now);

#endif
