/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for using the procedures in btext.c.
 *
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 */
#ifndef __PPC_BTEXT_H
#define __PPC_BTEXT_H
#ifdef __KERNEL__

extern int btext_find_display(int allow_nonstdout);
extern void btext_update_display(unsigned long phys, int width, int height,
				 int depth, int pitch);
extern void btext_setup_display(int width, int height, int depth, int pitch,
				unsigned long address);
#ifdef CONFIG_PPC32
extern void btext_prepare_BAT(void);
#else
static inline void btext_prepare_BAT(void) { }
#endif
extern void btext_map(void);
extern void btext_unmap(void);

extern void btext_drawchar(char c);
extern void btext_drawstring(const char *str);
extern void btext_drawhex(unsigned long v);
extern void btext_drawtext(const char *c, unsigned int len);

extern void btext_clearscreen(void);
extern void btext_flushscreen(void);
extern void btext_flushline(void);

#endif /* __KERNEL__ */
#endif /* __PPC_BTEXT_H */
