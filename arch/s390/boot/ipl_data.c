// SPDX-License-Identifier: GPL-2.0

#include <linux/compat.h>
#include <linux/ptrace.h>
#include <asm/cio.h>
#include <asm/asm-offsets.h>
#include "boot.h"

#define CCW0(cmd, addr, cnt, flg) \
	{ .cmd_code = cmd, .cda = addr, .count = cnt, .flags = flg, }

#define PSW_MASK_DISABLED (PSW_MASK_WAIT | PSW_MASK_EA | PSW_MASK_BA)

struct ipl_lowcore {
	psw_t32		ipl_psw;			/* 0x0000 */
	struct ccw0	ccwpgm[2];			/* 0x0008 */
	u8		fill[56];			/* 0x0018 */
	struct ccw0	ccwpgmcc[20];			/* 0x0050 */
	u8		pad_0xf0[0x0140-0x00f0];	/* 0x00f0 */
	psw_t		svc_old_psw;			/* 0x0140 */
	u8		pad_0x150[0x01a0-0x0150];	/* 0x0150 */
	psw_t		restart_psw;			/* 0x01a0 */
	psw_t		external_new_psw;		/* 0x01b0 */
	psw_t		svc_new_psw;			/* 0x01c0 */
	psw_t		program_new_psw;		/* 0x01d0 */
	psw_t		mcck_new_psw;			/* 0x01e0 */
	psw_t		io_new_psw;			/* 0x01f0 */
};

/*
 * Initial lowcore for IPL: the first 24 bytes are loaded by IPL to
 * addresses 0-23 (a PSW and two CCWs). Bytes 24-79 are discarded.
 * The next 160 bytes are loaded to addresses 0x18-0xb7. They form
 * the continuation of the CCW program started by IPL and load the
 * range 0x0f0-0x730 from the image to the range 0x0f0-0x730 in
 * memory. At the end of the channel program the PSW at location 0 is
 * loaded.
 * Initial processing starts at 0x200 = iplstart.
 *
 * The restart psw points to iplstart which allows to load a kernel
 * image into memory and starting it by a psw restart on any cpu. All
 * other default psw new locations contain a disabled wait psw where
 * the address indicates which psw was loaded.
 *
 * Note that the 'file' utility can detect s390 kernel images. For
 * that to succeed the two initial CCWs, and the 0x40 fill bytes must
 * be present.
 */
static struct ipl_lowcore ipl_lowcore __used __section(".ipldata") = {
	.ipl_psw = { .mask = PSW32_MASK_BASE, .addr = PSW32_ADDR_AMODE | IPL_START },
	.ccwpgm = {
		[ 0] = CCW0(CCW_CMD_READ_IPL, 0x018, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 1] = CCW0(CCW_CMD_READ_IPL, 0x068, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
	},
	.fill = {
		[ 0 ... 55] = 0x40,
	},
	.ccwpgmcc = {
		[ 0] = CCW0(CCW_CMD_READ_IPL, 0x0f0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 1] = CCW0(CCW_CMD_READ_IPL, 0x140, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 2] = CCW0(CCW_CMD_READ_IPL, 0x190, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 3] = CCW0(CCW_CMD_READ_IPL, 0x1e0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 4] = CCW0(CCW_CMD_READ_IPL, 0x230, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 5] = CCW0(CCW_CMD_READ_IPL, 0x280, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 6] = CCW0(CCW_CMD_READ_IPL, 0x2d0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 7] = CCW0(CCW_CMD_READ_IPL, 0x320, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 8] = CCW0(CCW_CMD_READ_IPL, 0x370, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[ 9] = CCW0(CCW_CMD_READ_IPL, 0x3c0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[10] = CCW0(CCW_CMD_READ_IPL, 0x410, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[11] = CCW0(CCW_CMD_READ_IPL, 0x460, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[12] = CCW0(CCW_CMD_READ_IPL, 0x4b0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[13] = CCW0(CCW_CMD_READ_IPL, 0x500, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[14] = CCW0(CCW_CMD_READ_IPL, 0x550, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[15] = CCW0(CCW_CMD_READ_IPL, 0x5a0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[16] = CCW0(CCW_CMD_READ_IPL, 0x5f0, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[17] = CCW0(CCW_CMD_READ_IPL, 0x640, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[18] = CCW0(CCW_CMD_READ_IPL, 0x690, 0x50, CCW_FLAG_SLI | CCW_FLAG_CC),
		[19] = CCW0(CCW_CMD_READ_IPL, 0x6e0, 0x50, CCW_FLAG_SLI),
	},
	/*
	 * Let the GDB's lx-symbols command find the jump_to_kernel symbol
	 * without having to load decompressor symbols.
	 */
	.svc_old_psw	  = { .mask = 0, .addr = (unsigned long)jump_to_kernel },
	.restart_psw	  = { .mask = 0, .addr = IPL_START, },
	.external_new_psw = { .mask = PSW_MASK_DISABLED, .addr = __LC_EXT_NEW_PSW, },
	.svc_new_psw	  = { .mask = PSW_MASK_DISABLED, .addr = __LC_SVC_NEW_PSW, },
	.program_new_psw  = { .mask = PSW_MASK_DISABLED, .addr = __LC_PGM_NEW_PSW, },
	.mcck_new_psw	  = { .mask = PSW_MASK_DISABLED, .addr = __LC_MCK_NEW_PSW, },
	.io_new_psw	  = { .mask = PSW_MASK_DISABLED, .addr = __LC_IO_NEW_PSW, },
};
