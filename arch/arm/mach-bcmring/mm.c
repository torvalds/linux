/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <linux/platform_device.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/csp/mm_io.h>

#define IO_DESC(va, sz) { .virtual = va, \
	.pfn = __phys_to_pfn(HW_IO_VIRT_TO_PHYS(va)), \
	.length = sz, \
	.type = MT_DEVICE }

#define MEM_DESC(va, sz) { .virtual = va, \
	.pfn = __phys_to_pfn(HW_IO_VIRT_TO_PHYS(va)), \
	.length = sz, \
	.type = MT_MEMORY }

static struct map_desc bcmring_io_desc[] __initdata = {
	IO_DESC(MM_IO_BASE_NAND, SZ_64K),	/* phys:0x28000000-0x28000FFF  virt:0xE8000000-0xE8000FFF  size:0x00010000 */
	IO_DESC(MM_IO_BASE_UMI, SZ_64K),	/* phys:0x2C000000-0x2C000FFF  virt:0xEC000000-0xEC000FFF  size:0x00010000 */

	IO_DESC(MM_IO_BASE_BROM, SZ_64K),	/* phys:0x30000000-0x3000FFFF  virt:0xF3000000-0xF300FFFF  size:0x00010000 */
	MEM_DESC(MM_IO_BASE_ARAM, SZ_1M),	/* phys:0x31000000-0x31FFFFFF  virt:0xF3100000-0xF31FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_DMA0, SZ_1M),	/* phys:0x32000000-0x32FFFFFF  virt:0xF3200000-0xF32FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_DMA1, SZ_1M),	/* phys:0x33000000-0x33FFFFFF  virt:0xF3300000-0xF33FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_ESW, SZ_1M),	/* phys:0x34000000-0x34FFFFFF  virt:0xF3400000-0xF34FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_CLCD, SZ_1M),	/* phys:0x35000000-0x35FFFFFF  virt:0xF3500000-0xF35FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_APM, SZ_1M),	/* phys:0x36000000-0x36FFFFFF  virt:0xF3600000-0xF36FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_SPUM, SZ_1M),	/* phys:0x37000000-0x37FFFFFF  virt:0xF3700000-0xF37FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_VPM_PROG, SZ_1M),	/* phys:0x38000000-0x38FFFFFF  virt:0xF3800000-0xF38FFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_VPM_DATA, SZ_1M),	/* phys:0x3A000000-0x3AFFFFFF  virt:0xF3A00000-0xF3AFFFFF  size:0x01000000 */

	IO_DESC(MM_IO_BASE_VRAM, SZ_64K),	/* phys:0x40000000-0x4000FFFF  virt:0xF4000000-0xF400FFFF  size:0x00010000 */
	IO_DESC(MM_IO_BASE_CHIPC, SZ_16M),	/* phys:0x80000000-0x80FFFFFF  virt:0xF8000000-0xF8FFFFFF  size:0x01000000 */
	IO_DESC(MM_IO_BASE_VPM_EXTMEM_RSVD,
		SZ_16M),	/* phys:0x0F000000-0x0FFFFFFF  virt:0xF0000000-0xF0FFFFFF  size:0x01000000 */
};

void __init bcmring_map_io(void)
{

	iotable_init(bcmring_io_desc, ARRAY_SIZE(bcmring_io_desc));
}
