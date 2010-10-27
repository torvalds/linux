/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef MDDIHOSTI_H
#define MDDIHOSTI_H

#include "msm_fb.h"
#include "mddihost.h"
#include <linux/clk.h>

/* Register offsets in MDDI, applies to both msm_pmdh_base and
 * (u32)msm_emdh_base. */
#define MDDI_CMD   		0x0000
#define MDDI_VERSION   		0x0004
#define MDDI_PRI_PTR		0x0008
#define MDDI_BPS		0x0010
#define MDDI_SPM		0x0014
#define MDDI_INT		0x0018
#define MDDI_INTEN		0x001c
#define MDDI_REV_PTR		0x0020
#define MDDI_REV_SIZE		0x0024
#define MDDI_STAT		0x0028
#define MDDI_REV_RATE_DIV	0x002c
#define MDDI_REV_CRC_ERR	0x0030
#define MDDI_TA1_LEN		0x0034
#define MDDI_TA2_LEN		0x0038
#define MDDI_TEST		0x0040
#define MDDI_REV_PKT_CNT	0x0044
#define MDDI_DRIVE_HI		0x0048
#define MDDI_DRIVE_LO		0x004c
#define MDDI_DISP_WAKE		0x0050
#define MDDI_REV_ENCAP_SZ	0x0054
#define MDDI_RTD_VAL		0x0058
#define MDDI_PAD_CTL		0x0068
#define MDDI_DRIVER_START_CNT	0x006c
#define MDDI_CORE_VER		0x008c
#define MDDI_FIFO_ALLOC         0x0090
#define MDDI_PAD_IO_CTL         0x00a0
#define MDDI_PAD_CAL            0x00a4

extern u32 mddi_msg_level;

/* No longer need to write to clear these registers */
#define xxxx_mddi_host_reg_outm(reg, mask, val)  \
do { \
	if (host_idx == MDDI_HOST_PRIM) \
		mddi_host_reg_outm_pmdh(reg, mask, val); \
	else \
		mddi_host_reg_outm_emdh(reg, mask, val); \
} while (0)

#define mddi_host_reg_outm(reg, mask, val) \
do { \
	unsigned long __addr; \
	if (host_idx == MDDI_HOST_PRIM) \
		__addr = (u32)msm_pmdh_base + MDDI_##reg; \
	else \
		__addr = (u32)msm_emdh_base + MDDI_##reg; \
	writel((readl(__addr) & ~(mask)) | ((val) & (mask)), __addr); \
} while (0)

#define xxxx_mddi_host_reg_out(reg, val) \
do { \
	if (host_idx == MDDI_HOST_PRIM)  \
		mddi_host_reg_out_pmdh(reg, val); \
	else \
		mddi_host_reg_out_emdh(reg, val); \
	} while (0)

#define mddi_host_reg_out(reg, val) \
do { \
	if (host_idx == MDDI_HOST_PRIM) \
		writel(val, (u32)msm_pmdh_base + MDDI_##reg); \
	else \
		writel(val, (u32)msm_emdh_base + MDDI_##reg); \
} while (0)

#define xxxx_mddi_host_reg_in(reg)  \
  ((host_idx) ? \
     mddi_host_reg_in_emdh(reg) : mddi_host_reg_in_pmdh(reg));

#define mddi_host_reg_in(reg) \
((host_idx) ? \
	readl((u32)msm_emdh_base + MDDI_##reg) : \
	readl((u32)msm_pmdh_base + MDDI_##reg)) \

#define xxxx_mddi_host_reg_inm(reg, mask)  \
  ((host_idx) ? \
    mddi_host_reg_inm_emdh(reg, mask) : \
    mddi_host_reg_inm_pmdh(reg, mask);)

#define mddi_host_reg_inm(reg, mask) \
((host_idx) ? \
	readl((u32)msm_emdh_base + MDDI_##reg) & (mask) : \
	readl((u32)msm_pmdh_base + MDDI_##reg) & (mask)) \

/* Using non-cacheable pmem, so do nothing */
#define mddi_invalidate_cache_lines(addr_start, num_bytes)
/*
 * Using non-cacheable pmem, so do nothing with cache
 * but, ensure write goes out to memory
 */
#define mddi_flush_cache_lines(addr_start, num_bytes)  \
    (void) addr_start; \
    (void) num_bytes;  \
    memory_barrier()

/* Since this translates to Remote Procedure Calls to check on clock status
* just use a local variable to keep track of io_clock */
#define MDDI_HOST_IS_IO_CLOCK_ON mddi_host_io_clock_on
#define MDDI_HOST_ENABLE_IO_CLOCK
#define MDDI_HOST_DISABLE_IO_CLOCK
#define MDDI_HOST_IS_HCLK_ON mddi_host_hclk_on
#define MDDI_HOST_ENABLE_HCLK
#define MDDI_HOST_DISABLE_HCLK
#define FEATURE_MDDI_HOST_IO_CLOCK_CONTROL_DISABLE
#define FEATURE_MDDI_HOST_HCLK_CONTROL_DISABLE

#define TRAMP_MDDI_HOST_ISR TRAMP_MDDI_PRI_ISR
#define TRAMP_MDDI_HOST_EXT_ISR TRAMP_MDDI_EXT_ISR
#define MDP_LINE_COUNT_BMSK  0x3ff
#define MDP_SYNC_STATUS  0x000c
#define MDP_LINE_COUNT      \
(readl(msm_mdp_base + MDP_SYNC_STATUS) & MDP_LINE_COUNT_BMSK)

/* MDP sends 256 pixel packets, so lower value hibernates more without
* significantly increasing latency of waiting for next subframe */
#define MDDI_HOST_BYTES_PER_SUBFRAME  0x3C00

#if defined(CONFIG_FB_MSM_MDP31) || defined(CONFIG_FB_MSM_MDP40)
#define MDDI_HOST_TA2_LEN       0x001a
#define MDDI_HOST_REV_RATE_DIV  0x0004
#else
#define MDDI_HOST_TA2_LEN       0x000c
#define MDDI_HOST_REV_RATE_DIV  0x0002
#endif

#define MDDI_MSG_EMERG(msg, ...)    \
	if (mddi_msg_level > 0)  \
		printk(KERN_EMERG msg, ## __VA_ARGS__);
#define MDDI_MSG_ALERT(msg, ...)    \
	if (mddi_msg_level > 1)  \
		printk(KERN_ALERT msg, ## __VA_ARGS__);
#define MDDI_MSG_CRIT(msg, ...)    \
	if (mddi_msg_level > 2)  \
		printk(KERN_CRIT msg, ## __VA_ARGS__);
#define MDDI_MSG_ERR(msg, ...)    \
	if (mddi_msg_level > 3)  \
		printk(KERN_ERR msg, ## __VA_ARGS__);
#define MDDI_MSG_WARNING(msg, ...)    \
	if (mddi_msg_level > 4)  \
		printk(KERN_WARNING msg, ## __VA_ARGS__);
#define MDDI_MSG_NOTICE(msg, ...)    \
	if (mddi_msg_level > 5)  \
		printk(KERN_NOTICE msg, ## __VA_ARGS__);
#define MDDI_MSG_INFO(msg, ...)    \
	if (mddi_msg_level > 6)  \
		printk(KERN_INFO msg, ## __VA_ARGS__);
#define MDDI_MSG_DEBUG(msg, ...)    \
	if (mddi_msg_level > 7)  \
		printk(KERN_DEBUG msg, ## __VA_ARGS__);

#define GCC_PACKED __attribute__((packed))
typedef struct GCC_PACKED {
	uint16 packet_length;
	/* total # of bytes in the packet not including
		the packet_length field. */

	uint16 packet_type;
	/* A Packet Type of 70 identifies the packet as
		a Client status Packet. */

	uint16 bClient_ID;
	/* This field is reserved for future use and shall
		be set to zero. */

} mddi_rev_packet_type;

typedef struct GCC_PACKED {
	uint16 packet_length;
	/* total # of bytes in the packet not including
		the packet_length field. */

	uint16 packet_type;
	/* A Packet Type of 70 identifies the packet as
		a Client status Packet. */

	uint16 bClient_ID;
	/* This field is reserved for future use and shall
		be set to zero. */

	uint16 reverse_link_request;
	/* 16 bit unsigned integer with number of bytes client
		needs in the * reverse encapsulation message
		to transmit data. */

	uint8 crc_error_count;
	uint8 capability_change;
	uint16 graphics_busy_flags;

	uint16 parameter_CRC;
	/* 16-bit CRC of all the bytes in the packet
		including Packet Length. */

} mddi_client_status_type;

typedef struct GCC_PACKED {
	uint16 packet_length;
	/* total # of bytes in the packet not including
		the packet_length field. */

	uint16 packet_type;
	/* A Packet Type of 66 identifies the packet as
		a Client Capability Packet. */

	uint16 bClient_ID;
	/* This field is reserved for future use and
		shall be set to zero. */

	uint16 Protocol_Version;
	uint16 Minimum_Protocol_Version;
	uint16 Data_Rate_Capability;
	uint8 Interface_Type_Capability;
	uint8 Number_of_Alt_Displays;
	uint16 PostCal_Data_Rate;
	uint16 Bitmap_Width;
	uint16 Bitmap_Height;
	uint16 Display_Window_Width;
	uint16 Display_Window_Height;
	uint32 Color_Map_Size;
	uint16 Color_Map_RGB_Width;
	uint16 RGB_Capability;
	uint8 Monochrome_Capability;
	uint8 Reserved_1;
	uint16 Y_Cb_Cr_Capability;
	uint16 Bayer_Capability;
	uint16 Alpha_Cursor_Image_Planes;
	uint32 Client_Feature_Capability_Indicators;
	uint8 Maximum_Video_Frame_Rate_Capability;
	uint8 Minimum_Video_Frame_Rate_Capability;
	uint16 Minimum_Sub_frame_Rate;
	uint16 Audio_Buffer_Depth;
	uint16 Audio_Channel_Capability;
	uint16 Audio_Sample_Rate_Capability;
	uint8 Audio_Sample_Resolution;
	uint8 Mic_Audio_Sample_Resolution;
	uint16 Mic_Sample_Rate_Capability;
	uint8 Keyboard_Data_Format;
	uint8 pointing_device_data_format;
	uint16 content_protection_type;
	uint16 Mfr_Name;
	uint16 Product_Code;
	uint16 Reserved_3;
	uint32 Serial_Number;
	uint8 Week_of_Manufacture;
	uint8 Year_of_Manufacture;

	uint16 parameter_CRC;
	/* 16-bit CRC of all the bytes in the packet including Packet Length. */

} mddi_client_capability_type;

typedef struct GCC_PACKED {
	uint16 packet_length;
	/* total # of bytes in the packet not including the packet_length field. */

	uint16 packet_type;
	/* A Packet Type of 16 identifies the packet as a Video Stream Packet. */

	uint16 bClient_ID;
	/* This field is reserved for future use and shall be set to zero. */

	uint16 video_data_format_descriptor;
	/* format of each pixel in the Pixel Data in the present stream in the
	 * present packet.
	 * If bits [15:13] = 000 monochrome
	 * If bits [15:13] = 001 color pixels (palette).
	 * If bits [15:13] = 010 color pixels in raw RGB
	 * If bits [15:13] = 011 data in 4:2:2 Y Cb Cr format
	 * If bits [15:13] = 100 Bayer pixels
	 */

	uint16 pixel_data_attributes;
	/* interpreted as follows:
	 * Bits [1:0] = 11  pixel data is displayed to both eyes
	 * Bits [1:0] = 10  pixel data is routed to the left eye only.
	 * Bits [1:0] = 01  pixel data is routed to the right eye only.
	 * Bits [1:0] = 00  pixel data is routed to the alternate display.
	 * Bit 2 is 0  Pixel Data is in the standard progressive format.
	 * Bit 2 is 1  Pixel Data is in interlace format.
	 * Bit 3 is 0  Pixel Data is in the standard progressive format.
	 * Bit 3 is 1  Pixel Data is in alternate pixel format.
	 * Bit 4 is 0  Pixel Data is to or from the display frame buffer.
	 * Bit 4 is 1  Pixel Data is to or from the camera.
	 * Bit 5 is 0  pixel data contains the next consecutive row of pixels.
	 * Bit 5 is 1  X Left Edge, Y Top Edge, X Right Edge, Y Bottom Edge,
	 *             X Start, and Y Start parameters are not defined and
	 *             shall be ignored by the client.
	 * Bits [7:6] = 01  Pixel data is written to the offline image buffer.
	 * Bits [7:6] = 00  Pixel data is written to the buffer to refresh display.
	 * Bits [7:6] = 11  Pixel data is written to all image buffers.
	 * Bits [7:6] = 10  Invalid. Reserved for future use.
	 * Bits 8 through 11 alternate display number.
	 * Bits 12 through 14 are reserved for future use and shall be set to zero.
	 * Bit 15 is 1 the row of pixels is the last row of pixels in a frame.
	 */

	uint16 x_left_edge;
	uint16 y_top_edge;
	/* X,Y coordinate of the top left edge of the screen window */

	uint16 x_right_edge;
	uint16 y_bottom_edge;
	/*  X,Y coordinate of the bottom right edge of the window being updated. */

	uint16 x_start;
	uint16 y_start;
	/*  (X Start, Y Start) is the first pixel in the Pixel Data field below. */

	uint16 pixel_count;
	/*  number of pixels in the Pixel Data field below. */

	uint16 parameter_CRC;
	/*  16-bit CRC of all bytes from the Packet Length to the Pixel Count. */

	uint16 reserved;
	/* 16-bit variable to make structure align on 4 byte boundary */

} mddi_video_stream_packet_type;

typedef struct GCC_PACKED {
	uint16 packet_length;
	/* total # of bytes in the packet not including the packet_length field. */

	uint16 packet_type;
	/* A Packet Type of 146 identifies the packet as a Register Access Packet. */

	uint16 bClient_ID;
	/* This field is reserved for future use and shall be set to zero. */

	uint16 read_write_info;
	/* Bits 13:0  a 14-bit unsigned integer that specifies the number of
	 *            32-bit Register Data List items to be transferred in the
	 *            Register Data List field.
	 * Bits[15:14] = 00  Write to register(s);
	 * Bits[15:14] = 10  Read from register(s);
	 * Bits[15:14] = 11  Response to a Read.
	 * Bits[15:14] = 01  this value is reserved for future use. */

	uint32 register_address;
	/* the register address that is to be written to or read from. */

	uint16 parameter_CRC;
	/* 16-bit CRC of all bytes from the Packet Length to the Register Address. */

	uint32 register_data_list;
	/* list of 4-byte register data values for/from client registers */

} mddi_register_access_packet_type;

typedef union GCC_PACKED {
	mddi_video_stream_packet_type video_pkt;
	mddi_register_access_packet_type register_pkt;
	/* add 48 byte pad to ensure 64 byte llist struct, that can be
	 * manipulated easily with cache */
	uint32 alignment_pad[12];	/* 48 bytes */
} mddi_packet_header_type;

typedef struct GCC_PACKED mddi_host_llist_struct {
	uint16 link_controller_flags;
	uint16 packet_header_count;
	uint16 packet_data_count;
	void *packet_data_pointer;
	struct mddi_host_llist_struct *next_packet_pointer;
	uint16 reserved;
	mddi_packet_header_type packet_header;
} mddi_linked_list_type;

typedef struct {
	struct completion done_comp;
	mddi_llist_done_cb_type done_cb;
	uint16 next_idx;
	boolean waiting;
	boolean in_use;
} mddi_linked_list_notify_type;

#define MDDI_LLIST_POOL_SIZE 0x1000
#define MDDI_MAX_NUM_LLIST_ITEMS (MDDI_LLIST_POOL_SIZE / \
		 sizeof(mddi_linked_list_type))
#define UNASSIGNED_INDEX MDDI_MAX_NUM_LLIST_ITEMS
#define MDDI_FIRST_DYNAMIC_LLIST_IDX 0

/* Static llist items can be used for applications that frequently send
 * the same set of packets using the linked list interface. */
/* Here we configure for 6 static linked list items:
 *  The 1st is used for a the adaptive backlight setting.
 *  and the remaining 5 are used for sending window adjustments for
 *  MDDI clients that need windowing info sent separate from video
 *  packets. */
#define MDDI_NUM_STATIC_ABL_ITEMS 1
#define MDDI_NUM_STATIC_WINDOW_ITEMS 5
#define MDDI_NUM_STATIC_LLIST_ITEMS (MDDI_NUM_STATIC_ABL_ITEMS + \
				MDDI_NUM_STATIC_WINDOW_ITEMS)
#define MDDI_NUM_DYNAMIC_LLIST_ITEMS (MDDI_MAX_NUM_LLIST_ITEMS - \
				MDDI_NUM_STATIC_LLIST_ITEMS)

#define MDDI_FIRST_STATIC_LLIST_IDX  MDDI_NUM_DYNAMIC_LLIST_ITEMS
#define MDDI_FIRST_STATIC_ABL_IDX  MDDI_FIRST_STATIC_LLIST_IDX
#define MDDI_FIRST_STATIC_WINDOW_IDX  (MDDI_FIRST_STATIC_LLIST_IDX + \
				MDDI_NUM_STATIC_ABL_ITEMS)

/* GPIO registers */
#define VSYNC_WAKEUP_REG          0x80
#define GPIO_REG                  0x81
#define GPIO_OUTPUT_REG           0x82
#define GPIO_INTERRUPT_REG        0x83
#define GPIO_INTERRUPT_ENABLE_REG 0x84
#define GPIO_POLARITY_REG         0x85

/* Interrupt Bits */
#define MDDI_INT_PRI_PTR_READ       0x0001
#define MDDI_INT_SEC_PTR_READ       0x0002
#define MDDI_INT_REV_DATA_AVAIL     0x0004
#define MDDI_INT_DISP_REQ           0x0008
#define MDDI_INT_PRI_UNDERFLOW      0x0010
#define MDDI_INT_SEC_UNDERFLOW      0x0020
#define MDDI_INT_REV_OVERFLOW       0x0040
#define MDDI_INT_CRC_ERROR          0x0080
#define MDDI_INT_MDDI_IN            0x0100
#define MDDI_INT_PRI_OVERWRITE      0x0200
#define MDDI_INT_SEC_OVERWRITE      0x0400
#define MDDI_INT_REV_OVERWRITE      0x0800
#define MDDI_INT_DMA_FAILURE        0x1000
#define MDDI_INT_LINK_ACTIVE        0x2000
#define MDDI_INT_IN_HIBERNATION     0x4000
#define MDDI_INT_PRI_LINK_LIST_DONE 0x8000
#define MDDI_INT_SEC_LINK_LIST_DONE 0x10000
#define MDDI_INT_NO_CMD_PKTS_PEND   0x20000
#define MDDI_INT_RTD_FAILURE        0x40000

#define MDDI_INT_ERROR_CONDITIONS ( \
	MDDI_INT_PRI_UNDERFLOW | MDDI_INT_SEC_UNDERFLOW | \
	MDDI_INT_REV_OVERFLOW | MDDI_INT_CRC_ERROR | \
	MDDI_INT_PRI_OVERWRITE | MDDI_INT_SEC_OVERWRITE | \
	MDDI_INT_RTD_FAILURE | \
	MDDI_INT_REV_OVERWRITE | MDDI_INT_DMA_FAILURE)

#define MDDI_INT_LINK_STATE_CHANGES ( \
	MDDI_INT_LINK_ACTIVE | MDDI_INT_IN_HIBERNATION)

/* Status Bits */
#define MDDI_STAT_LINK_ACTIVE        0x0001
#define MDDI_STAT_NEW_REV_PTR        0x0002
#define MDDI_STAT_NEW_PRI_PTR        0x0004
#define MDDI_STAT_NEW_SEC_PTR        0x0008
#define MDDI_STAT_IN_HIBERNATION     0x0010
#define MDDI_STAT_PRI_LINK_LIST_DONE 0x0020
#define MDDI_STAT_SEC_LINK_LIST_DONE 0x0040
#define MDDI_STAT_PENDING_TIMING_PKT 0x0080
#define MDDI_STAT_PENDING_REV_ENCAP  0x0100
#define MDDI_STAT_PENDING_POWERDOWN  0x0200
#define MDDI_STAT_RTD_MEAS_FAIL      0x0800
#define MDDI_STAT_CLIENT_WAKEUP_REQ  0x1000

/* Command Bits */
#define MDDI_CMD_POWERDOWN           0x0100
#define MDDI_CMD_POWERUP             0x0200
#define MDDI_CMD_HIBERNATE           0x0300
#define MDDI_CMD_RESET               0x0400
#define MDDI_CMD_DISP_IGNORE         0x0501
#define MDDI_CMD_DISP_LISTEN         0x0500
#define MDDI_CMD_SEND_REV_ENCAP      0x0600
#define MDDI_CMD_GET_CLIENT_CAP      0x0601
#define MDDI_CMD_GET_CLIENT_STATUS   0x0602
#define MDDI_CMD_SEND_RTD            0x0700
#define MDDI_CMD_LINK_ACTIVE         0x0900
#define MDDI_CMD_PERIODIC_REV_ENCAP  0x0A00

extern void mddi_host_init(mddi_host_type host);
extern void mddi_host_powerdown(mddi_host_type host);
extern uint16 mddi_get_next_free_llist_item(mddi_host_type host, boolean wait);
extern uint16 mddi_get_reg_read_llist_item(mddi_host_type host, boolean wait);
extern void mddi_queue_forward_packets(uint16 first_llist_idx,
				       uint16 last_llist_idx,
				       boolean wait,
				       mddi_llist_done_cb_type llist_done_cb,
				       mddi_host_type host);

extern void mddi_host_write_pix_attr_reg(uint32 value);
extern void mddi_client_lcd_gpio_poll(uint32 poll_reg_val);
extern void mddi_client_lcd_vsync_detected(boolean detected);
extern void mddi_host_disable_hibernation(boolean disable);

extern mddi_linked_list_type *llist_extern[];
extern mddi_linked_list_type *llist_dma_extern[];
extern mddi_linked_list_notify_type *llist_extern_notify[];
extern struct timer_list mddi_host_timer;

typedef struct {
	uint16 transmitting_start_idx;
	uint16 transmitting_end_idx;
	uint16 waiting_start_idx;
	uint16 waiting_end_idx;
	uint16 reg_read_idx;
	uint16 next_free_idx;
	boolean reg_read_waiting;
} mddi_llist_info_type;

extern mddi_llist_info_type mddi_llist;

#define MDDI_GPIO_DEFAULT_POLLING_INTERVAL 200
typedef struct {
	uint32 polling_reg;
	uint32 polling_val;
	uint32 polling_interval;
	boolean polling_enabled;
} mddi_gpio_info_type;

uint32 mddi_get_client_id(void);
void mddi_mhctl_remove(mddi_host_type host_idx);
void mddi_host_timer_service(unsigned long data);
#endif /* MDDIHOSTI_H */
