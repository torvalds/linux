#ifndef __LINUX_FBIO_H
#define __LINUX_FBIO_H

#include <uapi/asm/fbio.h>

#define FBIOPUTCMAP_SPARC _IOW('F', 3, struct fbcmap)
#define FBIOGETCMAP_SPARC _IOW('F', 4, struct fbcmap)
/* Addresses on the fd of a cgsix that are mappable */
#define CG6_FBC    0x70000000
#define CG6_TEC    0x70001000
#define CG6_BTREGS 0x70002000
#define CG6_FHC    0x70004000
#define CG6_THC    0x70005000
#define CG6_ROM    0x70006000
#define CG6_RAM    0x70016000
#define CG6_DHC    0x80000000

#define CG3_MMAP_OFFSET 0x4000000

/* Addresses on the fd of a tcx that are mappable */
#define TCX_RAM8BIT   		0x00000000
#define TCX_RAM24BIT   		0x01000000
#define TCX_UNK3   		0x10000000
#define TCX_UNK4   		0x20000000
#define TCX_CONTROLPLANE   	0x28000000
#define TCX_UNK6   		0x30000000
#define TCX_UNK7   		0x38000000
#define TCX_TEC    		0x70000000
#define TCX_BTREGS 		0x70002000
#define TCX_THC    		0x70004000
#define TCX_DHC    		0x70008000
#define TCX_ALT	   		0x7000a000
#define TCX_SYNC   		0x7000e000
#define TCX_UNK2    		0x70010000

/* CG14 definitions */

/* Offsets into the OBIO space: */
#define CG14_REGS        0       /* registers */
#define CG14_CURSORREGS  0x1000  /* cursor registers */
#define CG14_DACREGS     0x2000  /* DAC registers */
#define CG14_XLUT        0x3000  /* X Look Up Table -- ??? */
#define CG14_CLUT1       0x4000  /* Color Look Up Table */
#define CG14_CLUT2       0x5000  /* Color Look Up Table */
#define CG14_CLUT3       0x6000  /* Color Look Up Table */
#define CG14_AUTO	 0xf000

struct  fbcmap32 {
	int             index;          /* first element (0 origin) */
	int             count;
	u32		red;
	u32		green;
	u32		blue;
};

#define FBIOPUTCMAP32	_IOW('F', 3, struct fbcmap32)
#define FBIOGETCMAP32	_IOW('F', 4, struct fbcmap32)

struct fbcursor32 {
	short set;		/* what to set, choose from the list above */
	short enable;		/* cursor on/off */
	struct fbcurpos pos;	/* cursor position */
	struct fbcurpos hot;	/* cursor hot spot */
	struct fbcmap32 cmap;	/* color map info */
	struct fbcurpos size;	/* cursor bit map size */
	u32	image;		/* cursor image bits */
	u32	mask;		/* cursor mask bits */
};

#define FBIOSCURSOR32	_IOW('F', 24, struct fbcursor32)
#define FBIOGCURSOR32	_IOW('F', 25, struct fbcursor32)
#endif /* __LINUX_FBIO_H */
