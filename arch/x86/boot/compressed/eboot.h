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

typedef union efi_uga_draw_protocol efi_uga_draw_protocol_t;

union efi_uga_draw_protocol {
	struct {
		efi_status_t (__efiapi *get_mode)(efi_uga_draw_protocol_t *,
						  u32*, u32*, u32*, u32*);
		void *set_mode;
		void *blt;
	};
	struct {
		u32 get_mode;
		u32 set_mode;
		u32 blt;
	} mixed_mode;
};

#endif /* BOOT_COMPRESSED_EBOOT_H */
