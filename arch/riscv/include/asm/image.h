/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_IMAGE_H
#define __ASM_IMAGE_H

#define RISCV_IMAGE_MAGIC	"RISCV"

#define RISCV_IMAGE_FLAG_BE_SHIFT	0
#define RISCV_IMAGE_FLAG_BE_MASK	0x1

#define RISCV_IMAGE_FLAG_LE		0
#define RISCV_IMAGE_FLAG_BE		1

#ifdef CONFIG_CPU_BIG_ENDIAN
#error conversion of header fields to LE not yet implemented
#else
#define __HEAD_FLAG_BE		RISCV_IMAGE_FLAG_LE
#endif

#define __HEAD_FLAG(field)	(__HEAD_FLAG_##field << \
				RISCV_IMAGE_FLAG_##field##_SHIFT)

#define __HEAD_FLAGS		(__HEAD_FLAG(BE))

#define RISCV_HEADER_VERSION_MAJOR 0
#define RISCV_HEADER_VERSION_MINOR 1

#define RISCV_HEADER_VERSION (RISCV_HEADER_VERSION_MAJOR << 16 | \
			      RISCV_HEADER_VERSION_MINOR)

#ifndef __ASSEMBLY__
/**
 * struct riscv_image_header - riscv kernel image header
 * @code0:		Executable code
 * @code1:		Executable code
 * @text_offset:	Image load offset (little endian)
 * @image_size:		Effective Image size (little endian)
 * @flags:		kernel flags (little endian)
 * @version:		version
 * @res1:		reserved
 * @res2:		reserved
 * @magic:		Magic number
 * @res3:		reserved (will be used for additional RISC-V specific
 *			header)
 * @res4:		reserved (will be used for PE COFF offset)
 *
 * The intention is for this header format to be shared between multiple
 * architectures to avoid a proliferation of image header formats.
 */

struct riscv_image_header {
	u32 code0;
	u32 code1;
	u64 text_offset;
	u64 image_size;
	u64 flags;
	u32 version;
	u32 res1;
	u64 res2;
	u64 magic;
	u32 res3;
	u32 res4;
};
#endif /* __ASSEMBLY__ */
#endif /* __ASM_IMAGE_H */
