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

#define EFI_PAGE_SIZE		(1UL << EFI_PAGE_SHIFT)
#define EFI_READ_CHUNK_SIZE	(1024 * 1024)

#define PIXEL_RGB_RESERVED_8BIT_PER_COLOR		0
#define PIXEL_BGR_RESERVED_8BIT_PER_COLOR		1
#define PIXEL_BIT_MASK					2
#define PIXEL_BLT_ONLY					3
#define PIXEL_FORMAT_MAX				4

struct efi_pixel_bitmask {
	u32 red_mask;
	u32 green_mask;
	u32 blue_mask;
	u32 reserved_mask;
};

struct efi_graphics_output_mode_info {
	u32 version;
	u32 horizontal_resolution;
	u32 vertical_resolution;
	int pixel_format;
	struct efi_pixel_bitmask pixel_information;
	u32 pixels_per_scan_line;
} __packed;

struct efi_graphics_output_protocol_mode {
	u32 max_mode;
	u32 mode;
	unsigned long info;
	unsigned long size_of_info;
	u64 frame_buffer_base;
	unsigned long frame_buffer_size;
} __packed;

struct efi_graphics_output_protocol {
	void *query_mode;
	unsigned long set_mode;
	unsigned long blt;
	struct efi_graphics_output_protocol_mode *mode;
};

struct efi_uga_draw_protocol {
	void *get_mode;
	void *set_mode;
	void *blt;
};

#endif /* BOOT_COMPRESSED_EBOOT_H */
