/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_COMPRESSED_EBOOT_H
#define BOOT_COMPRESSED_EBOOT_H

#define SEG_TYPE_DATA		(0 << 3)
#define SEG_TYPE_READ_WRITE	(1 << 1)
#define SEG_TYPE_CODE		(1 << 3)
#define SEG_TYPE_EXEC_READ	(1 << 1)
#define SEG_TYPE_TSS		((1 << 3) | (1 << 0))
#define SEG_OP_SIZE_32BIT	(1 << 0)
#define SEG_GRANULARITY_4KB	(1 << 0)

#define DESC_TYPE_CODE_DATA	(1 << 0)

typedef struct {
	u32 get_mode;
	u32 set_mode;
	u32 blt;
} efi_uga_draw_protocol_32_t;

typedef struct {
	u64 get_mode;
	u64 set_mode;
	u64 blt;
} efi_uga_draw_protocol_64_t;

typedef struct {
	void *get_mode;
	void *set_mode;
	void *blt;
} efi_uga_draw_protocol_t;

#endif /* BOOT_COMPRESSED_EBOOT_H */
