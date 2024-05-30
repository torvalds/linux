// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Bluetooth HCI driver for Broadcom 4377/4378/4387 devices attached via PCIe
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#include <linux/async.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/printk.h>

#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

enum bcm4377_chip {
	BCM4377 = 0,
	BCM4378,
	BCM4387,
};

#define BCM4377_DEVICE_ID 0x5fa0
#define BCM4378_DEVICE_ID 0x5f69
#define BCM4387_DEVICE_ID 0x5f71

#define BCM4377_TIMEOUT 1000

/*
 * These devices only support DMA transactions inside a 32bit window
 * (possibly to avoid 64 bit arithmetic). The window size cannot exceed
 * 0xffffffff but is always aligned down to the previous 0x200 byte boundary
 * which effectively limits the window to [start, start+0xfffffe00].
 * We just limit the DMA window to [0, 0xfffffe00] to make sure we don't
 * run into this limitation.
 */
#define BCM4377_DMA_MASK 0xfffffe00

#define BCM4377_PCIECFG_BAR0_WINDOW1	   0x80
#define BCM4377_PCIECFG_BAR0_WINDOW2	   0x70
#define BCM4377_PCIECFG_BAR0_CORE2_WINDOW1 0x74
#define BCM4377_PCIECFG_BAR0_CORE2_WINDOW2 0x78
#define BCM4377_PCIECFG_BAR2_WINDOW	   0x84

#define BCM4377_PCIECFG_BAR0_CORE2_WINDOW1_DEFAULT 0x18011000
#define BCM4377_PCIECFG_BAR2_WINDOW_DEFAULT	   0x19000000

#define BCM4377_PCIECFG_SUBSYSTEM_CTRL 0x88

#define BCM4377_BAR0_FW_DOORBELL 0x140
#define BCM4377_BAR0_RTI_CONTROL 0x144

#define BCM4377_BAR0_SLEEP_CONTROL	      0x150
#define BCM4377_BAR0_SLEEP_CONTROL_UNQUIESCE  0
#define BCM4377_BAR0_SLEEP_CONTROL_AWAKE      2
#define BCM4377_BAR0_SLEEP_CONTROL_QUIESCE    3

#define BCM4377_BAR0_DOORBELL	    0x174
#define BCM4377_BAR0_DOORBELL_VALUE GENMASK(31, 16)
#define BCM4377_BAR0_DOORBELL_IDX   GENMASK(15, 8)
#define BCM4377_BAR0_DOORBELL_RING  BIT(5)

#define BCM4377_BAR0_HOST_WINDOW_LO   0x590
#define BCM4377_BAR0_HOST_WINDOW_HI   0x594
#define BCM4377_BAR0_HOST_WINDOW_SIZE 0x598

#define BCM4377_BAR2_BOOTSTAGE 0x200454

#define BCM4377_BAR2_FW_LO   0x200478
#define BCM4377_BAR2_FW_HI   0x20047c
#define BCM4377_BAR2_FW_SIZE 0x200480

#define BCM4377_BAR2_CONTEXT_ADDR_LO 0x20048c
#define BCM4377_BAR2_CONTEXT_ADDR_HI 0x200450

#define BCM4377_BAR2_RTI_STATUS	     0x20045c
#define BCM4377_BAR2_RTI_WINDOW_LO   0x200494
#define BCM4377_BAR2_RTI_WINDOW_HI   0x200498
#define BCM4377_BAR2_RTI_WINDOW_SIZE 0x20049c

#define BCM4377_OTP_SIZE	  0xe0
#define BCM4377_OTP_SYS_VENDOR	  0x15
#define BCM4377_OTP_CIS		  0x80
#define BCM4377_OTP_VENDOR_HDR	  0x00000008
#define BCM4377_OTP_MAX_PARAM_LEN 16

#define BCM4377_N_TRANSFER_RINGS   9
#define BCM4377_N_COMPLETION_RINGS 6

#define BCM4377_MAX_RING_SIZE 256

#define BCM4377_MSGID_GENERATION GENMASK(15, 8)
#define BCM4377_MSGID_ID	 GENMASK(7, 0)

#define BCM4377_RING_N_ENTRIES 128

#define BCM4377_CONTROL_MSG_SIZE		   0x34
#define BCM4377_XFER_RING_MAX_INPLACE_PAYLOAD_SIZE (4 * 0xff)

#define MAX_ACL_PAYLOAD_SIZE   (HCI_MAX_FRAME_SIZE + HCI_ACL_HDR_SIZE)
#define MAX_SCO_PAYLOAD_SIZE   (HCI_MAX_SCO_SIZE + HCI_SCO_HDR_SIZE)
#define MAX_EVENT_PAYLOAD_SIZE (HCI_MAX_EVENT_SIZE + HCI_EVENT_HDR_SIZE)

enum bcm4377_otp_params_type {
	BCM4377_OTP_BOARD_PARAMS,
	BCM4377_OTP_CHIP_PARAMS
};

enum bcm4377_transfer_ring_id {
	BCM4377_XFER_RING_CONTROL = 0,
	BCM4377_XFER_RING_HCI_H2D = 1,
	BCM4377_XFER_RING_HCI_D2H = 2,
	BCM4377_XFER_RING_SCO_H2D = 3,
	BCM4377_XFER_RING_SCO_D2H = 4,
	BCM4377_XFER_RING_ACL_H2D = 5,
	BCM4377_XFER_RING_ACL_D2H = 6,
};

enum bcm4377_completion_ring_id {
	BCM4377_ACK_RING_CONTROL = 0,
	BCM4377_ACK_RING_HCI_ACL = 1,
	BCM4377_EVENT_RING_HCI_ACL = 2,
	BCM4377_ACK_RING_SCO = 3,
	BCM4377_EVENT_RING_SCO = 4,
};

enum bcm4377_doorbell {
	BCM4377_DOORBELL_CONTROL = 0,
	BCM4377_DOORBELL_HCI_H2D = 1,
	BCM4377_DOORBELL_HCI_D2H = 2,
	BCM4377_DOORBELL_ACL_H2D = 3,
	BCM4377_DOORBELL_ACL_D2H = 4,
	BCM4377_DOORBELL_SCO = 6,
};

/*
 * Transfer ring entry
 *
 * flags: Flags to indicate if the payload is appended or mapped
 * len: Payload length
 * payload: Optional payload DMA address
 * id: Message id to recognize the answer in the completion ring entry
 */
struct bcm4377_xfer_ring_entry {
#define BCM4377_XFER_RING_FLAG_PAYLOAD_MAPPED	 BIT(0)
#define BCM4377_XFER_RING_FLAG_PAYLOAD_IN_FOOTER BIT(1)
	u8 flags;
	__le16 len;
	u8 _unk0;
	__le64 payload;
	__le16 id;
	u8 _unk1[2];
} __packed;
static_assert(sizeof(struct bcm4377_xfer_ring_entry) == 0x10);

/*
 * Completion ring entry
 *
 * flags: Flags to indicate if the payload is appended or mapped. If the payload
 *        is mapped it can be found in the buffer of the corresponding transfer
 *        ring message.
 * ring_id: Transfer ring ID which required this message
 * msg_id: Message ID specified in transfer ring entry
 * len: Payload length
 */
struct bcm4377_completion_ring_entry {
	u8 flags;
	u8 _unk0;
	__le16 ring_id;
	__le16 msg_id;
	__le32 len;
	u8 _unk1[6];
} __packed;
static_assert(sizeof(struct bcm4377_completion_ring_entry) == 0x10);

enum bcm4377_control_message_type {
	BCM4377_CONTROL_MSG_CREATE_XFER_RING = 1,
	BCM4377_CONTROL_MSG_CREATE_COMPLETION_RING = 2,
	BCM4377_CONTROL_MSG_DESTROY_XFER_RING = 3,
	BCM4377_CONTROL_MSG_DESTROY_COMPLETION_RING = 4,
};

/*
 * Control message used to create a completion ring
 *
 * msg_type: Must be BCM4377_CONTROL_MSG_CREATE_COMPLETION_RING
 * header_size: Unknown, but probably reserved space in front of the entry
 * footer_size: Number of 32 bit words reserved for payloads after the entry
 * id/id_again: Completion ring index
 * ring_iova: DMA address of the ring buffer
 * n_elements: Number of elements inside the ring buffer
 * msi: MSI index, doesn't work for all rings though and should be zero
 * intmod_delay: Unknown delay
 * intmod_bytes: Unknown
 */
struct bcm4377_create_completion_ring_msg {
	u8 msg_type;
	u8 header_size;
	u8 footer_size;
	u8 _unk0;
	__le16 id;
	__le16 id_again;
	__le64 ring_iova;
	__le16 n_elements;
	__le32 unk;
	u8 _unk1[6];
	__le16 msi;
	__le16 intmod_delay;
	__le32 intmod_bytes;
	__le16 _unk2;
	__le32 _unk3;
	u8 _unk4[10];
} __packed;
static_assert(sizeof(struct bcm4377_create_completion_ring_msg) ==
	      BCM4377_CONTROL_MSG_SIZE);

/*
 * Control ring message used to destroy a completion ring
 *
 * msg_type: Must be BCM4377_CONTROL_MSG_DESTROY_COMPLETION_RING
 * ring_id: Completion ring to be destroyed
 */
struct bcm4377_destroy_completion_ring_msg {
	u8 msg_type;
	u8 _pad0;
	__le16 ring_id;
	u8 _pad1[48];
} __packed;
static_assert(sizeof(struct bcm4377_destroy_completion_ring_msg) ==
	      BCM4377_CONTROL_MSG_SIZE);

/*
 * Control message used to create a transfer ring
 *
 * msg_type: Must be BCM4377_CONTROL_MSG_CREATE_XFER_RING
 * header_size: Number of 32 bit words reserved for unknown content before the
 *              entry
 * footer_size: Number of 32 bit words reserved for payloads after the entry
 * ring_id/ring_id_again: Transfer ring index
 * ring_iova: DMA address of the ring buffer
 * n_elements: Number of elements inside the ring buffer
 * completion_ring_id: Completion ring index for acknowledgements and events
 * doorbell: Doorbell index used to notify device of new entries
 * flags: Transfer ring flags
 *          - virtual: set if there is no associated shared memory and only the
 *                     corresponding completion ring is used
 *          - sync: only set for the SCO rings
 */
struct bcm4377_create_transfer_ring_msg {
	u8 msg_type;
	u8 header_size;
	u8 footer_size;
	u8 _unk0;
	__le16 ring_id;
	__le16 ring_id_again;
	__le64 ring_iova;
	u8 _unk1[8];
	__le16 n_elements;
	__le16 completion_ring_id;
	__le16 doorbell;
#define BCM4377_XFER_RING_FLAG_VIRTUAL BIT(7)
#define BCM4377_XFER_RING_FLAG_SYNC    BIT(8)
	__le16 flags;
	u8 _unk2[20];
} __packed;
static_assert(sizeof(struct bcm4377_create_transfer_ring_msg) ==
	      BCM4377_CONTROL_MSG_SIZE);

/*
 * Control ring message used to destroy a transfer ring
 *
 * msg_type: Must be BCM4377_CONTROL_MSG_DESTROY_XFER_RING
 * ring_id: Transfer ring to be destroyed
 */
struct bcm4377_destroy_transfer_ring_msg {
	u8 msg_type;
	u8 _pad0;
	__le16 ring_id;
	u8 _pad1[48];
} __packed;
static_assert(sizeof(struct bcm4377_destroy_transfer_ring_msg) ==
	      BCM4377_CONTROL_MSG_SIZE);

/*
 * "Converged IPC" context struct used to make the device aware of all other
 * shared memory structures. A pointer to this structure is configured inside a
 * MMIO register.
 *
 * version: Protocol version, must be 2.
 * size: Size of this structure, must be 0x68.
 * enabled_caps: Enabled capabilities. Unknown bitfield but should be 2.
 * peripheral_info_addr: DMA address for a 0x20 buffer to which the device will
 *                       write unknown contents
 * {completion,xfer}_ring_{tails,heads}_addr: DMA pointers to ring heads/tails
 * n_completion_rings: Number of completion rings, the firmware only works if
 *                     this is set to BCM4377_N_COMPLETION_RINGS.
 * n_xfer_rings: Number of transfer rings, the firmware only works if
 *               this is set to BCM4377_N_TRANSFER_RINGS.
 * control_completion_ring_addr: Control completion ring buffer DMA address
 * control_xfer_ring_addr: Control transfer ring buffer DMA address
 * control_xfer_ring_n_entries: Number of control transfer ring entries
 * control_completion_ring_n_entries: Number of control completion ring entries
 * control_xfer_ring_doorbell: Control transfer ring doorbell
 * control_completion_ring_doorbell: Control completion ring doorbell,
 *                                   must be set to 0xffff
 * control_xfer_ring_msi: Control completion ring MSI index, must be 0
 * control_completion_ring_msi: Control completion ring MSI index, must be 0.
 * control_xfer_ring_header_size: Number of 32 bit words reserved in front of
 *                                every control transfer ring entry
 * control_xfer_ring_footer_size: Number of 32 bit words reserved after every
 *                                control transfer ring entry
 * control_completion_ring_header_size: Number of 32 bit words reserved in front
 *                                      of every control completion ring entry
 * control_completion_ring_footer_size: Number of 32 bit words reserved after
 *                                      every control completion ring entry
 * scratch_pad: Optional scratch pad DMA address
 * scratch_pad_size: Scratch pad size
 */
struct bcm4377_context {
	__le16 version;
	__le16 size;
	__le32 enabled_caps;

	__le64 peripheral_info_addr;

	/* ring heads and tails */
	__le64 completion_ring_heads_addr;
	__le64 xfer_ring_tails_addr;
	__le64 completion_ring_tails_addr;
	__le64 xfer_ring_heads_addr;
	__le16 n_completion_rings;
	__le16 n_xfer_rings;

	/* control ring configuration */
	__le64 control_completion_ring_addr;
	__le64 control_xfer_ring_addr;
	__le16 control_xfer_ring_n_entries;
	__le16 control_completion_ring_n_entries;
	__le16 control_xfer_ring_doorbell;
	__le16 control_completion_ring_doorbell;
	__le16 control_xfer_ring_msi;
	__le16 control_completion_ring_msi;
	u8 control_xfer_ring_header_size;
	u8 control_xfer_ring_footer_size;
	u8 control_completion_ring_header_size;
	u8 control_completion_ring_footer_size;

	__le16 _unk0;
	__le16 _unk1;

	__le64 scratch_pad;
	__le32 scratch_pad_size;

	__le32 _unk3;
} __packed;
static_assert(sizeof(struct bcm4377_context) == 0x68);

#define BCM4378_CALIBRATION_CHUNK_SIZE 0xe6
struct bcm4378_hci_send_calibration_cmd {
	u8 unk;
	__le16 blocks_left;
	u8 data[BCM4378_CALIBRATION_CHUNK_SIZE];
} __packed;

#define BCM4378_PTB_CHUNK_SIZE 0xcf
struct bcm4378_hci_send_ptb_cmd {
	__le16 blocks_left;
	u8 data[BCM4378_PTB_CHUNK_SIZE];
} __packed;

/*
 * Shared memory structure used to store the ring head and tail pointers.
 */
struct bcm4377_ring_state {
	__le16 completion_ring_head[BCM4377_N_COMPLETION_RINGS];
	__le16 completion_ring_tail[BCM4377_N_COMPLETION_RINGS];
	__le16 xfer_ring_head[BCM4377_N_TRANSFER_RINGS];
	__le16 xfer_ring_tail[BCM4377_N_TRANSFER_RINGS];
};

/*
 * A transfer ring can be used in two configurations:
 *  1) Send control or HCI messages to the device which are then acknowledged
 *     in the corresponding completion ring
 *  2) Receiving HCI frames from the devices. In this case the transfer ring
 *     itself contains empty messages that are acknowledged once data is
 *     available from the device. If the payloads fit inside the footers
 *     of the completion ring the transfer ring can be configured to be
 *     virtual such that it has no ring buffer.
 *
 * ring_id: ring index hardcoded in the firmware
 * doorbell: doorbell index to notify device of new entries
 * payload_size: optional in-place payload size
 * mapped_payload_size: optional out-of-place payload size
 * completion_ring: index of corresponding completion ring
 * n_entries: number of entries inside this ring
 * generation: ring generation; incremented on hci_open to detect stale messages
 * sync: set to true for SCO rings
 * virtual: set to true if this ring has no entries and is just required to
 *          setup a corresponding completion ring for device->host messages
 * d2h_buffers_only: set to true if this ring is only used to provide large
 *                   buffers used by device->host messages in the completion
 *                   ring
 * allow_wait: allow to wait for messages to be acknowledged
 * enabled: true once the ring has been created and can be used
 * ring: ring buffer for entries (struct bcm4377_xfer_ring_entry)
 * ring_dma: DMA address for ring entry buffer
 * payloads: payload buffer for mapped_payload_size payloads
 * payloads_dma:DMA address for payload buffer
 * events: pointer to array of completions if waiting is allowed
 * msgids: bitmap to keep track of used message ids
 * lock: Spinlock to protect access to ring structurs used in the irq handler
 */
struct bcm4377_transfer_ring {
	enum bcm4377_transfer_ring_id ring_id;
	enum bcm4377_doorbell doorbell;
	size_t payload_size;
	size_t mapped_payload_size;
	u8 completion_ring;
	u16 n_entries;
	u8 generation;

	bool sync;
	bool virtual;
	bool d2h_buffers_only;
	bool allow_wait;
	bool enabled;

	void *ring;
	dma_addr_t ring_dma;

	void *payloads;
	dma_addr_t payloads_dma;

	struct completion **events;
	DECLARE_BITMAP(msgids, BCM4377_MAX_RING_SIZE);
	spinlock_t lock;
};

/*
 * A completion ring can be either used to either acknowledge messages sent in
 * the corresponding transfer ring or to receive messages associated with the
 * transfer ring. When used to receive messages the transfer ring either
 * has no ring buffer and is only advanced ("virtual transfer ring") or it
 * only contains empty DMA buffers to be used for the payloads.
 *
 * ring_id: completion ring id, hardcoded in firmware
 * payload_size: optional payload size after each entry
 * delay: unknown delay
 * n_entries: number of entries in this ring
 * enabled: true once the ring has been created and can be used
 * ring: ring buffer for entries (struct bcm4377_completion_ring_entry)
 * ring_dma: DMA address of ring buffer
 * transfer_rings: bitmap of corresponding transfer ring ids
 */
struct bcm4377_completion_ring {
	enum bcm4377_completion_ring_id ring_id;
	u16 payload_size;
	u16 delay;
	u16 n_entries;
	bool enabled;

	void *ring;
	dma_addr_t ring_dma;

	unsigned long transfer_rings;
};

struct bcm4377_data;

/*
 * Chip-specific configuration struct
 *
 * id: Chip id (e.g. 0x4377 for BCM4377)
 * otp_offset: Offset to the start of the OTP inside BAR0
 * bar0_window1: Backplane address mapped to the first window in BAR0
 * bar0_window2: Backplane address mapped to the second window in BAR0
 * bar0_core2_window2: Optional backplane address mapped to the second core's
 *                     second window in BAR0
 * has_bar0_core2_window2: Set to true if this chip requires the second core's
 *                         second window to be configured
 * clear_pciecfg_subsystem_ctrl_bit19: Set to true if bit 19 in the
 *                                     vendor-specific subsystem control
 *                                     register has to be cleared
 * disable_aspm: Set to true if ASPM must be disabled due to hardware errata
 * broken_ext_scan: Set to true if the chip erroneously claims to support
 *                  extended scanning
 * broken_mws_transport_config: Set to true if the chip erroneously claims to
 *                              support MWS Transport Configuration
 * send_calibration: Optional callback to send calibration data
 * send_ptb: Callback to send "PTB" regulatory/calibration data
 */
struct bcm4377_hw {
	unsigned int id;

	u32 otp_offset;

	u32 bar0_window1;
	u32 bar0_window2;
	u32 bar0_core2_window2;

	unsigned long has_bar0_core2_window2 : 1;
	unsigned long clear_pciecfg_subsystem_ctrl_bit19 : 1;
	unsigned long disable_aspm : 1;
	unsigned long broken_ext_scan : 1;
	unsigned long broken_mws_transport_config : 1;
	unsigned long broken_le_coded : 1;

	int (*send_calibration)(struct bcm4377_data *bcm4377);
	int (*send_ptb)(struct bcm4377_data *bcm4377,
			const struct firmware *fw);
};

static const struct bcm4377_hw bcm4377_hw_variants[];
static const struct dmi_system_id bcm4377_dmi_board_table[];

/*
 * Private struct associated with each device containing global state
 *
 * pdev: Pointer to associated struct pci_dev
 * hdev: Pointer to associated strucy hci_dev
 * bar0: iomem pointing to BAR0
 * bar1: iomem pointing to BAR2
 * bootstage: Current value of the bootstage
 * rti_status: Current "RTI" status value
 * hw: Pointer to chip-specific struct bcm4377_hw
 * taurus_cal_blob: "Taurus" calibration blob used for some chips
 * taurus_cal_size: "Taurus" calibration blob size
 * taurus_beamforming_cal_blob: "Taurus" beamforming calibration blob used for
 *                              some chips
 * taurus_beamforming_cal_size: "Taurus" beamforming calibration blob size
 * stepping: Chip stepping read from OTP; used for firmware selection
 * vendor: Antenna vendor read from OTP; used for firmware selection
 * board_type: Board type from FDT or DMI match; used for firmware selection
 * event: Event for changed bootstage or rti_status; used for booting firmware
 * ctx: "Converged IPC" context
 * ctx_dma: "Converged IPC" context DMA address
 * ring_state: Shared memory buffer containing ring head and tail indexes
 * ring_state_dma: DMA address for ring_state
 * {control,hci_acl,sco}_ack_ring: Completion rings used to acknowledge messages
 * {hci_acl,sco}_event_ring: Completion rings used for device->host messages
 * control_h2d_ring: Transfer ring used for control messages
 * {hci,sco,acl}_h2d_ring: Transfer ring used to transfer HCI frames
 * {hci,sco,acl}_d2h_ring: Transfer ring used to receive HCI frames in the
 *                         corresponding completion ring
 */
struct bcm4377_data {
	struct pci_dev *pdev;
	struct hci_dev *hdev;

	void __iomem *bar0;
	void __iomem *bar2;

	u32 bootstage;
	u32 rti_status;

	const struct bcm4377_hw *hw;

	const void *taurus_cal_blob;
	int taurus_cal_size;
	const void *taurus_beamforming_cal_blob;
	int taurus_beamforming_cal_size;

	char stepping[BCM4377_OTP_MAX_PARAM_LEN];
	char vendor[BCM4377_OTP_MAX_PARAM_LEN];
	const char *board_type;

	struct completion event;

	struct bcm4377_context *ctx;
	dma_addr_t ctx_dma;

	struct bcm4377_ring_state *ring_state;
	dma_addr_t ring_state_dma;

	/*
	 * The HCI and ACL rings have to be merged because this structure is
	 * hardcoded in the firmware.
	 */
	struct bcm4377_completion_ring control_ack_ring;
	struct bcm4377_completion_ring hci_acl_ack_ring;
	struct bcm4377_completion_ring hci_acl_event_ring;
	struct bcm4377_completion_ring sco_ack_ring;
	struct bcm4377_completion_ring sco_event_ring;

	struct bcm4377_transfer_ring control_h2d_ring;
	struct bcm4377_transfer_ring hci_h2d_ring;
	struct bcm4377_transfer_ring hci_d2h_ring;
	struct bcm4377_transfer_ring sco_h2d_ring;
	struct bcm4377_transfer_ring sco_d2h_ring;
	struct bcm4377_transfer_ring acl_h2d_ring;
	struct bcm4377_transfer_ring acl_d2h_ring;
};

static void bcm4377_ring_doorbell(struct bcm4377_data *bcm4377, u8 doorbell,
				  u16 val)
{
	u32 db = 0;

	db |= FIELD_PREP(BCM4377_BAR0_DOORBELL_VALUE, val);
	db |= FIELD_PREP(BCM4377_BAR0_DOORBELL_IDX, doorbell);
	db |= BCM4377_BAR0_DOORBELL_RING;

	dev_dbg(&bcm4377->pdev->dev, "write %d to doorbell #%d (0x%x)\n", val,
		doorbell, db);
	iowrite32(db, bcm4377->bar0 + BCM4377_BAR0_DOORBELL);
}

static int bcm4377_extract_msgid(struct bcm4377_data *bcm4377,
				 struct bcm4377_transfer_ring *ring,
				 u16 raw_msgid, u8 *msgid)
{
	u8 generation = FIELD_GET(BCM4377_MSGID_GENERATION, raw_msgid);
	*msgid = FIELD_GET(BCM4377_MSGID_ID, raw_msgid);

	if (generation != ring->generation) {
		dev_warn(
			&bcm4377->pdev->dev,
			"invalid message generation %d should be %d in entry for ring %d\n",
			generation, ring->generation, ring->ring_id);
		return -EINVAL;
	}

	if (*msgid >= ring->n_entries) {
		dev_warn(&bcm4377->pdev->dev,
			 "invalid message id in entry for ring %d: %d > %d\n",
			 ring->ring_id, *msgid, ring->n_entries);
		return -EINVAL;
	}

	return 0;
}

static void bcm4377_handle_event(struct bcm4377_data *bcm4377,
				 struct bcm4377_transfer_ring *ring,
				 u16 raw_msgid, u8 entry_flags, u8 type,
				 void *payload, size_t len)
{
	struct sk_buff *skb;
	u16 head;
	u8 msgid;
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	if (!ring->enabled) {
		dev_warn(&bcm4377->pdev->dev,
			 "event for disabled transfer ring %d\n",
			 ring->ring_id);
		goto out;
	}

	if (ring->d2h_buffers_only &&
	    entry_flags & BCM4377_XFER_RING_FLAG_PAYLOAD_MAPPED) {
		if (bcm4377_extract_msgid(bcm4377, ring, raw_msgid, &msgid))
			goto out;

		if (len > ring->mapped_payload_size) {
			dev_warn(
				&bcm4377->pdev->dev,
				"invalid payload len in event for ring %d: %zu > %zu\n",
				ring->ring_id, len, ring->mapped_payload_size);
			goto out;
		}

		payload = ring->payloads + msgid * ring->mapped_payload_size;
	}

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb)
		goto out;

	memcpy(skb_put(skb, len), payload, len);
	hci_skb_pkt_type(skb) = type;
	hci_recv_frame(bcm4377->hdev, skb);

out:
	head = le16_to_cpu(bcm4377->ring_state->xfer_ring_head[ring->ring_id]);
	head = (head + 1) % ring->n_entries;
	bcm4377->ring_state->xfer_ring_head[ring->ring_id] = cpu_to_le16(head);

	bcm4377_ring_doorbell(bcm4377, ring->doorbell, head);

	spin_unlock_irqrestore(&ring->lock, flags);
}

static void bcm4377_handle_ack(struct bcm4377_data *bcm4377,
			       struct bcm4377_transfer_ring *ring,
			       u16 raw_msgid)
{
	unsigned long flags;
	u8 msgid;

	spin_lock_irqsave(&ring->lock, flags);

	if (bcm4377_extract_msgid(bcm4377, ring, raw_msgid, &msgid))
		goto unlock;

	if (!test_bit(msgid, ring->msgids)) {
		dev_warn(
			&bcm4377->pdev->dev,
			"invalid message id in ack for ring %d: %d is not used\n",
			ring->ring_id, msgid);
		goto unlock;
	}

	if (ring->allow_wait && ring->events[msgid]) {
		complete(ring->events[msgid]);
		ring->events[msgid] = NULL;
	}

	bitmap_release_region(ring->msgids, msgid, ring->n_entries);

unlock:
	spin_unlock_irqrestore(&ring->lock, flags);
}

static void bcm4377_handle_completion(struct bcm4377_data *bcm4377,
				      struct bcm4377_completion_ring *ring,
				      u16 pos)
{
	struct bcm4377_completion_ring_entry *entry;
	u16 msg_id, transfer_ring;
	size_t entry_size, data_len;
	void *data;

	if (pos >= ring->n_entries) {
		dev_warn(&bcm4377->pdev->dev,
			 "invalid offset %d for completion ring %d\n", pos,
			 ring->ring_id);
		return;
	}

	entry_size = sizeof(*entry) + ring->payload_size;
	entry = ring->ring + pos * entry_size;
	data = ring->ring + pos * entry_size + sizeof(*entry);
	data_len = le32_to_cpu(entry->len);
	msg_id = le16_to_cpu(entry->msg_id);
	transfer_ring = le16_to_cpu(entry->ring_id);

	if ((ring->transfer_rings & BIT(transfer_ring)) == 0) {
		dev_warn(
			&bcm4377->pdev->dev,
			"invalid entry at offset %d for transfer ring %d in completion ring %d\n",
			pos, transfer_ring, ring->ring_id);
		return;
	}

	dev_dbg(&bcm4377->pdev->dev,
		"entry in completion ring %d for transfer ring %d with msg_id %d\n",
		ring->ring_id, transfer_ring, msg_id);

	switch (transfer_ring) {
	case BCM4377_XFER_RING_CONTROL:
		bcm4377_handle_ack(bcm4377, &bcm4377->control_h2d_ring, msg_id);
		break;
	case BCM4377_XFER_RING_HCI_H2D:
		bcm4377_handle_ack(bcm4377, &bcm4377->hci_h2d_ring, msg_id);
		break;
	case BCM4377_XFER_RING_SCO_H2D:
		bcm4377_handle_ack(bcm4377, &bcm4377->sco_h2d_ring, msg_id);
		break;
	case BCM4377_XFER_RING_ACL_H2D:
		bcm4377_handle_ack(bcm4377, &bcm4377->acl_h2d_ring, msg_id);
		break;

	case BCM4377_XFER_RING_HCI_D2H:
		bcm4377_handle_event(bcm4377, &bcm4377->hci_d2h_ring, msg_id,
				     entry->flags, HCI_EVENT_PKT, data,
				     data_len);
		break;
	case BCM4377_XFER_RING_SCO_D2H:
		bcm4377_handle_event(bcm4377, &bcm4377->sco_d2h_ring, msg_id,
				     entry->flags, HCI_SCODATA_PKT, data,
				     data_len);
		break;
	case BCM4377_XFER_RING_ACL_D2H:
		bcm4377_handle_event(bcm4377, &bcm4377->acl_d2h_ring, msg_id,
				     entry->flags, HCI_ACLDATA_PKT, data,
				     data_len);
		break;

	default:
		dev_warn(
			&bcm4377->pdev->dev,
			"entry in completion ring %d for unknown transfer ring %d with msg_id %d\n",
			ring->ring_id, transfer_ring, msg_id);
	}
}

static void bcm4377_poll_completion_ring(struct bcm4377_data *bcm4377,
					 struct bcm4377_completion_ring *ring)
{
	u16 tail;
	__le16 *heads = bcm4377->ring_state->completion_ring_head;
	__le16 *tails = bcm4377->ring_state->completion_ring_tail;

	if (!ring->enabled)
		return;

	tail = le16_to_cpu(tails[ring->ring_id]);
	dev_dbg(&bcm4377->pdev->dev,
		"completion ring #%d: head: %d, tail: %d\n", ring->ring_id,
		le16_to_cpu(heads[ring->ring_id]), tail);

	while (tail != le16_to_cpu(READ_ONCE(heads[ring->ring_id]))) {
		/*
		 * ensure the CPU doesn't speculate through the comparison.
		 * otherwise it might already read the (empty) queue entry
		 * before the updated head has been loaded and checked.
		 */
		dma_rmb();

		bcm4377_handle_completion(bcm4377, ring, tail);

		tail = (tail + 1) % ring->n_entries;
		tails[ring->ring_id] = cpu_to_le16(tail);
	}
}

static irqreturn_t bcm4377_irq(int irq, void *data)
{
	struct bcm4377_data *bcm4377 = data;
	u32 bootstage, rti_status;

	bootstage = ioread32(bcm4377->bar2 + BCM4377_BAR2_BOOTSTAGE);
	rti_status = ioread32(bcm4377->bar2 + BCM4377_BAR2_RTI_STATUS);

	if (bootstage != bcm4377->bootstage ||
	    rti_status != bcm4377->rti_status) {
		dev_dbg(&bcm4377->pdev->dev,
			"bootstage = %d -> %d, rti state = %d -> %d\n",
			bcm4377->bootstage, bootstage, bcm4377->rti_status,
			rti_status);
		complete(&bcm4377->event);
		bcm4377->bootstage = bootstage;
		bcm4377->rti_status = rti_status;
	}

	if (rti_status > 2)
		dev_err(&bcm4377->pdev->dev, "RTI status is %d\n", rti_status);

	bcm4377_poll_completion_ring(bcm4377, &bcm4377->control_ack_ring);
	bcm4377_poll_completion_ring(bcm4377, &bcm4377->hci_acl_event_ring);
	bcm4377_poll_completion_ring(bcm4377, &bcm4377->hci_acl_ack_ring);
	bcm4377_poll_completion_ring(bcm4377, &bcm4377->sco_ack_ring);
	bcm4377_poll_completion_ring(bcm4377, &bcm4377->sco_event_ring);

	return IRQ_HANDLED;
}

static int bcm4377_enqueue(struct bcm4377_data *bcm4377,
			   struct bcm4377_transfer_ring *ring, void *data,
			   size_t len, bool wait)
{
	unsigned long flags;
	struct bcm4377_xfer_ring_entry *entry;
	void *payload;
	size_t offset;
	u16 head, tail, new_head;
	u16 raw_msgid;
	int ret, msgid;
	DECLARE_COMPLETION_ONSTACK(event);

	if (len > ring->payload_size && len > ring->mapped_payload_size) {
		dev_warn(
			&bcm4377->pdev->dev,
			"payload len %zu is too large for ring %d (max is %zu or %zu)\n",
			len, ring->ring_id, ring->payload_size,
			ring->mapped_payload_size);
		return -EINVAL;
	}
	if (wait && !ring->allow_wait)
		return -EINVAL;
	if (ring->virtual)
		return -EINVAL;

	spin_lock_irqsave(&ring->lock, flags);

	head = le16_to_cpu(bcm4377->ring_state->xfer_ring_head[ring->ring_id]);
	tail = le16_to_cpu(bcm4377->ring_state->xfer_ring_tail[ring->ring_id]);

	new_head = (head + 1) % ring->n_entries;

	if (new_head == tail) {
		dev_warn(&bcm4377->pdev->dev,
			 "can't send message because ring %d is full\n",
			 ring->ring_id);
		ret = -EINVAL;
		goto out;
	}

	msgid = bitmap_find_free_region(ring->msgids, ring->n_entries, 0);
	if (msgid < 0) {
		dev_warn(&bcm4377->pdev->dev,
			 "can't find message id for ring %d\n", ring->ring_id);
		ret = -EINVAL;
		goto out;
	}

	raw_msgid = FIELD_PREP(BCM4377_MSGID_GENERATION, ring->generation);
	raw_msgid |= FIELD_PREP(BCM4377_MSGID_ID, msgid);

	offset = head * (sizeof(*entry) + ring->payload_size);
	entry = ring->ring + offset;

	memset(entry, 0, sizeof(*entry));
	entry->id = cpu_to_le16(raw_msgid);
	entry->len = cpu_to_le16(len);

	if (len <= ring->payload_size) {
		entry->flags = BCM4377_XFER_RING_FLAG_PAYLOAD_IN_FOOTER;
		payload = ring->ring + offset + sizeof(*entry);
	} else {
		entry->flags = BCM4377_XFER_RING_FLAG_PAYLOAD_MAPPED;
		entry->payload = cpu_to_le64(ring->payloads_dma +
					     msgid * ring->mapped_payload_size);
		payload = ring->payloads + msgid * ring->mapped_payload_size;
	}

	memcpy(payload, data, len);

	if (wait)
		ring->events[msgid] = &event;

	/*
	 * The 4377 chips stop responding to any commands as soon as they
	 * have been idle for a while. Poking the sleep control register here
	 * makes them come alive again.
	 */
	iowrite32(BCM4377_BAR0_SLEEP_CONTROL_AWAKE,
		  bcm4377->bar0 + BCM4377_BAR0_SLEEP_CONTROL);

	dev_dbg(&bcm4377->pdev->dev,
		"updating head for transfer queue #%d to %d\n", ring->ring_id,
		new_head);
	bcm4377->ring_state->xfer_ring_head[ring->ring_id] =
		cpu_to_le16(new_head);

	if (!ring->sync)
		bcm4377_ring_doorbell(bcm4377, ring->doorbell, new_head);
	ret = 0;

out:
	spin_unlock_irqrestore(&ring->lock, flags);

	if (ret == 0 && wait) {
		ret = wait_for_completion_interruptible_timeout(
			&event, BCM4377_TIMEOUT);
		if (ret == 0)
			ret = -ETIMEDOUT;
		else if (ret > 0)
			ret = 0;

		spin_lock_irqsave(&ring->lock, flags);
		ring->events[msgid] = NULL;
		spin_unlock_irqrestore(&ring->lock, flags);
	}

	return ret;
}

static int bcm4377_create_completion_ring(struct bcm4377_data *bcm4377,
					  struct bcm4377_completion_ring *ring)
{
	struct bcm4377_create_completion_ring_msg msg;
	int ret;

	if (ring->enabled) {
		dev_warn(&bcm4377->pdev->dev,
			 "completion ring %d already enabled\n", ring->ring_id);
		return 0;
	}

	memset(ring->ring, 0,
	       ring->n_entries * (sizeof(struct bcm4377_completion_ring_entry) +
				  ring->payload_size));
	memset(&msg, 0, sizeof(msg));
	msg.msg_type = BCM4377_CONTROL_MSG_CREATE_COMPLETION_RING;
	msg.id = cpu_to_le16(ring->ring_id);
	msg.id_again = cpu_to_le16(ring->ring_id);
	msg.ring_iova = cpu_to_le64(ring->ring_dma);
	msg.n_elements = cpu_to_le16(ring->n_entries);
	msg.intmod_bytes = cpu_to_le32(0xffffffff);
	msg.unk = cpu_to_le32(0xffffffff);
	msg.intmod_delay = cpu_to_le16(ring->delay);
	msg.footer_size = ring->payload_size / 4;

	ret = bcm4377_enqueue(bcm4377, &bcm4377->control_h2d_ring, &msg,
			      sizeof(msg), true);
	if (!ret)
		ring->enabled = true;

	return ret;
}

static int bcm4377_destroy_completion_ring(struct bcm4377_data *bcm4377,
					   struct bcm4377_completion_ring *ring)
{
	struct bcm4377_destroy_completion_ring_msg msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.msg_type = BCM4377_CONTROL_MSG_DESTROY_COMPLETION_RING;
	msg.ring_id = cpu_to_le16(ring->ring_id);

	ret = bcm4377_enqueue(bcm4377, &bcm4377->control_h2d_ring, &msg,
			      sizeof(msg), true);
	if (ret)
		dev_warn(&bcm4377->pdev->dev,
			 "failed to destroy completion ring %d\n",
			 ring->ring_id);

	ring->enabled = false;
	return ret;
}

static int bcm4377_create_transfer_ring(struct bcm4377_data *bcm4377,
					struct bcm4377_transfer_ring *ring)
{
	struct bcm4377_create_transfer_ring_msg msg;
	u16 flags = 0;
	int ret, i;
	unsigned long spinlock_flags;

	if (ring->virtual)
		flags |= BCM4377_XFER_RING_FLAG_VIRTUAL;
	if (ring->sync)
		flags |= BCM4377_XFER_RING_FLAG_SYNC;

	spin_lock_irqsave(&ring->lock, spinlock_flags);
	memset(&msg, 0, sizeof(msg));
	msg.msg_type = BCM4377_CONTROL_MSG_CREATE_XFER_RING;
	msg.ring_id = cpu_to_le16(ring->ring_id);
	msg.ring_id_again = cpu_to_le16(ring->ring_id);
	msg.ring_iova = cpu_to_le64(ring->ring_dma);
	msg.n_elements = cpu_to_le16(ring->n_entries);
	msg.completion_ring_id = cpu_to_le16(ring->completion_ring);
	msg.doorbell = cpu_to_le16(ring->doorbell);
	msg.flags = cpu_to_le16(flags);
	msg.footer_size = ring->payload_size / 4;

	bcm4377->ring_state->xfer_ring_head[ring->ring_id] = 0;
	bcm4377->ring_state->xfer_ring_tail[ring->ring_id] = 0;
	ring->generation++;
	spin_unlock_irqrestore(&ring->lock, spinlock_flags);

	ret = bcm4377_enqueue(bcm4377, &bcm4377->control_h2d_ring, &msg,
			      sizeof(msg), true);

	spin_lock_irqsave(&ring->lock, spinlock_flags);

	if (ring->d2h_buffers_only) {
		for (i = 0; i < ring->n_entries; ++i) {
			struct bcm4377_xfer_ring_entry *entry =
				ring->ring + i * sizeof(*entry);
			u16 raw_msgid = FIELD_PREP(BCM4377_MSGID_GENERATION,
						   ring->generation);
			raw_msgid |= FIELD_PREP(BCM4377_MSGID_ID, i);

			memset(entry, 0, sizeof(*entry));
			entry->id = cpu_to_le16(raw_msgid);
			entry->len = cpu_to_le16(ring->mapped_payload_size);
			entry->flags = BCM4377_XFER_RING_FLAG_PAYLOAD_MAPPED;
			entry->payload =
				cpu_to_le64(ring->payloads_dma +
					    i * ring->mapped_payload_size);
		}
	}

	/*
	 * send some messages if this is a device->host ring to allow the device
	 * to reply by acknowledging them in the completion ring
	 */
	if (ring->virtual || ring->d2h_buffers_only) {
		bcm4377->ring_state->xfer_ring_head[ring->ring_id] =
			cpu_to_le16(0xf);
		bcm4377_ring_doorbell(bcm4377, ring->doorbell, 0xf);
	}

	ring->enabled = true;
	spin_unlock_irqrestore(&ring->lock, spinlock_flags);

	return ret;
}

static int bcm4377_destroy_transfer_ring(struct bcm4377_data *bcm4377,
					 struct bcm4377_transfer_ring *ring)
{
	struct bcm4377_destroy_transfer_ring_msg msg;
	int ret;

	memset(&msg, 0, sizeof(msg));
	msg.msg_type = BCM4377_CONTROL_MSG_DESTROY_XFER_RING;
	msg.ring_id = cpu_to_le16(ring->ring_id);

	ret = bcm4377_enqueue(bcm4377, &bcm4377->control_h2d_ring, &msg,
			      sizeof(msg), true);
	if (ret)
		dev_warn(&bcm4377->pdev->dev,
			 "failed to destroy transfer ring %d\n", ring->ring_id);

	ring->enabled = false;
	return ret;
}

static int __bcm4378_send_calibration_chunk(struct bcm4377_data *bcm4377,
					    const void *data, size_t data_len,
					    u16 blocks_left)
{
	struct bcm4378_hci_send_calibration_cmd cmd;
	struct sk_buff *skb;

	if (data_len > sizeof(cmd.data))
		return -EINVAL;

	memset(&cmd, 0, sizeof(cmd));
	cmd.unk = 0x03;
	cmd.blocks_left = cpu_to_le16(blocks_left);
	memcpy(cmd.data, data, data_len);

	skb = __hci_cmd_sync(bcm4377->hdev, 0xfd97, sizeof(cmd), &cmd,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	kfree_skb(skb);
	return 0;
}

static int __bcm4378_send_calibration(struct bcm4377_data *bcm4377,
				      const void *data, size_t data_size)
{
	int ret;
	size_t i, left, transfer_len;
	size_t blocks =
		DIV_ROUND_UP(data_size, (size_t)BCM4378_CALIBRATION_CHUNK_SIZE);

	if (!data) {
		dev_err(&bcm4377->pdev->dev,
			"no calibration data available.\n");
		return -ENOENT;
	}

	for (i = 0, left = data_size; i < blocks; ++i, left -= transfer_len) {
		transfer_len =
			min_t(size_t, left, BCM4378_CALIBRATION_CHUNK_SIZE);

		ret = __bcm4378_send_calibration_chunk(
			bcm4377, data + i * BCM4378_CALIBRATION_CHUNK_SIZE,
			transfer_len, blocks - i - 1);
		if (ret) {
			dev_err(&bcm4377->pdev->dev,
				"send calibration chunk failed with %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int bcm4378_send_calibration(struct bcm4377_data *bcm4377)
{
	if ((strcmp(bcm4377->stepping, "b1") == 0) ||
	    strcmp(bcm4377->stepping, "b3") == 0)
		return __bcm4378_send_calibration(
			bcm4377, bcm4377->taurus_beamforming_cal_blob,
			bcm4377->taurus_beamforming_cal_size);
	else
		return __bcm4378_send_calibration(bcm4377,
						  bcm4377->taurus_cal_blob,
						  bcm4377->taurus_cal_size);
}

static int bcm4387_send_calibration(struct bcm4377_data *bcm4377)
{
	if (strcmp(bcm4377->stepping, "c2") == 0)
		return __bcm4378_send_calibration(
			bcm4377, bcm4377->taurus_beamforming_cal_blob,
			bcm4377->taurus_beamforming_cal_size);
	else
		return __bcm4378_send_calibration(bcm4377,
						  bcm4377->taurus_cal_blob,
						  bcm4377->taurus_cal_size);
}

static const struct firmware *bcm4377_request_blob(struct bcm4377_data *bcm4377,
						   const char *suffix)
{
	const struct firmware *fw;
	char name0[64], name1[64];
	int ret;

	snprintf(name0, sizeof(name0), "brcm/brcmbt%04x%s-%s-%s.%s",
		 bcm4377->hw->id, bcm4377->stepping, bcm4377->board_type,
		 bcm4377->vendor, suffix);
	snprintf(name1, sizeof(name1), "brcm/brcmbt%04x%s-%s.%s",
		 bcm4377->hw->id, bcm4377->stepping, bcm4377->board_type,
		 suffix);
	dev_dbg(&bcm4377->pdev->dev, "Trying to load firmware: '%s' or '%s'\n",
		name0, name1);

	ret = firmware_request_nowarn(&fw, name0, &bcm4377->pdev->dev);
	if (!ret)
		return fw;
	ret = firmware_request_nowarn(&fw, name1, &bcm4377->pdev->dev);
	if (!ret)
		return fw;

	dev_err(&bcm4377->pdev->dev,
		"Unable to load firmware; tried '%s' and '%s'\n", name0, name1);
	return NULL;
}

static int bcm4377_send_ptb(struct bcm4377_data *bcm4377,
			    const struct firmware *fw)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(bcm4377->hdev, 0xfd98, fw->size, fw->data,
			     HCI_INIT_TIMEOUT);
	/*
	 * This command seems to always fail on more recent firmware versions
	 * (even in traces taken from the macOS driver). It's unclear why this
	 * happens but because the PTB file contains calibration and/or
	 * regulatory data and may be required on older firmware we still try to
	 * send it here just in case and just ignore if it fails.
	 */
	if (!IS_ERR(skb))
		kfree_skb(skb);
	return 0;
}

static int bcm4378_send_ptb_chunk(struct bcm4377_data *bcm4377,
				  const void *data, size_t data_len,
				  u16 blocks_left)
{
	struct bcm4378_hci_send_ptb_cmd cmd;
	struct sk_buff *skb;

	if (data_len > BCM4378_PTB_CHUNK_SIZE)
		return -EINVAL;

	memset(&cmd, 0, sizeof(cmd));
	cmd.blocks_left = cpu_to_le16(blocks_left);
	memcpy(cmd.data, data, data_len);

	skb = __hci_cmd_sync(bcm4377->hdev, 0xfe0d, sizeof(cmd), &cmd,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	kfree_skb(skb);
	return 0;
}

static int bcm4378_send_ptb(struct bcm4377_data *bcm4377,
			    const struct firmware *fw)
{
	size_t chunks = DIV_ROUND_UP(fw->size, (size_t)BCM4378_PTB_CHUNK_SIZE);
	size_t i, left, transfer_len;
	int ret;

	for (i = 0, left = fw->size; i < chunks; ++i, left -= transfer_len) {
		transfer_len = min_t(size_t, left, BCM4378_PTB_CHUNK_SIZE);

		dev_dbg(&bcm4377->pdev->dev, "sending ptb chunk %zu/%zu\n",
			i + 1, chunks);
		ret = bcm4378_send_ptb_chunk(
			bcm4377, fw->data + i * BCM4378_PTB_CHUNK_SIZE,
			transfer_len, chunks - i - 1);
		if (ret) {
			dev_err(&bcm4377->pdev->dev,
				"sending ptb chunk %zu failed (%d)", i, ret);
			return ret;
		}
	}

	return 0;
}

static int bcm4377_hci_open(struct hci_dev *hdev)
{
	struct bcm4377_data *bcm4377 = hci_get_drvdata(hdev);
	int ret;

	dev_dbg(&bcm4377->pdev->dev, "creating rings\n");

	ret = bcm4377_create_completion_ring(bcm4377,
					     &bcm4377->hci_acl_ack_ring);
	if (ret)
		return ret;
	ret = bcm4377_create_completion_ring(bcm4377,
					     &bcm4377->hci_acl_event_ring);
	if (ret)
		goto destroy_hci_acl_ack;
	ret = bcm4377_create_completion_ring(bcm4377, &bcm4377->sco_ack_ring);
	if (ret)
		goto destroy_hci_acl_event;
	ret = bcm4377_create_completion_ring(bcm4377, &bcm4377->sco_event_ring);
	if (ret)
		goto destroy_sco_ack;
	dev_dbg(&bcm4377->pdev->dev,
		"all completion rings successfully created!\n");

	ret = bcm4377_create_transfer_ring(bcm4377, &bcm4377->hci_h2d_ring);
	if (ret)
		goto destroy_sco_event;
	ret = bcm4377_create_transfer_ring(bcm4377, &bcm4377->hci_d2h_ring);
	if (ret)
		goto destroy_hci_h2d;
	ret = bcm4377_create_transfer_ring(bcm4377, &bcm4377->sco_h2d_ring);
	if (ret)
		goto destroy_hci_d2h;
	ret = bcm4377_create_transfer_ring(bcm4377, &bcm4377->sco_d2h_ring);
	if (ret)
		goto destroy_sco_h2d;
	ret = bcm4377_create_transfer_ring(bcm4377, &bcm4377->acl_h2d_ring);
	if (ret)
		goto destroy_sco_d2h;
	ret = bcm4377_create_transfer_ring(bcm4377, &bcm4377->acl_d2h_ring);
	if (ret)
		goto destroy_acl_h2d;
	dev_dbg(&bcm4377->pdev->dev,
		"all transfer rings successfully created!\n");

	return 0;

destroy_acl_h2d:
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->acl_h2d_ring);
destroy_sco_d2h:
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->sco_d2h_ring);
destroy_sco_h2d:
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->sco_h2d_ring);
destroy_hci_d2h:
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->hci_h2d_ring);
destroy_hci_h2d:
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->hci_d2h_ring);
destroy_sco_event:
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->sco_event_ring);
destroy_sco_ack:
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->sco_ack_ring);
destroy_hci_acl_event:
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->hci_acl_event_ring);
destroy_hci_acl_ack:
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->hci_acl_ack_ring);

	dev_err(&bcm4377->pdev->dev, "Creating rings failed with %d\n", ret);
	return ret;
}

static int bcm4377_hci_close(struct hci_dev *hdev)
{
	struct bcm4377_data *bcm4377 = hci_get_drvdata(hdev);

	dev_dbg(&bcm4377->pdev->dev, "destroying rings in hci_close\n");

	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->acl_d2h_ring);
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->acl_h2d_ring);
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->sco_d2h_ring);
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->sco_h2d_ring);
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->hci_d2h_ring);
	bcm4377_destroy_transfer_ring(bcm4377, &bcm4377->hci_h2d_ring);

	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->sco_event_ring);
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->sco_ack_ring);
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->hci_acl_event_ring);
	bcm4377_destroy_completion_ring(bcm4377, &bcm4377->hci_acl_ack_ring);

	return 0;
}

static bool bcm4377_is_valid_bdaddr(struct bcm4377_data *bcm4377,
				    bdaddr_t *addr)
{
	if (addr->b[0] != 0x93)
		return true;
	if (addr->b[1] != 0x76)
		return true;
	if (addr->b[2] != 0x00)
		return true;
	if (addr->b[4] != (bcm4377->hw->id & 0xff))
		return true;
	if (addr->b[5] != (bcm4377->hw->id >> 8))
		return true;
	return false;
}

static int bcm4377_check_bdaddr(struct bcm4377_data *bcm4377)
{
	struct hci_rp_read_bd_addr *bda;
	struct sk_buff *skb;

	skb = __hci_cmd_sync(bcm4377->hdev, HCI_OP_READ_BD_ADDR, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);

		dev_err(&bcm4377->pdev->dev, "HCI_OP_READ_BD_ADDR failed (%d)",
			err);
		return err;
	}

	if (skb->len != sizeof(*bda)) {
		dev_err(&bcm4377->pdev->dev,
			"HCI_OP_READ_BD_ADDR reply length invalid");
		kfree_skb(skb);
		return -EIO;
	}

	bda = (struct hci_rp_read_bd_addr *)skb->data;
	if (!bcm4377_is_valid_bdaddr(bcm4377, &bda->bdaddr))
		set_bit(HCI_QUIRK_USE_BDADDR_PROPERTY, &bcm4377->hdev->quirks);

	kfree_skb(skb);
	return 0;
}

static int bcm4377_hci_setup(struct hci_dev *hdev)
{
	struct bcm4377_data *bcm4377 = hci_get_drvdata(hdev);
	const struct firmware *fw;
	int ret;

	if (bcm4377->hw->send_calibration) {
		ret = bcm4377->hw->send_calibration(bcm4377);
		if (ret)
			return ret;
	}

	fw = bcm4377_request_blob(bcm4377, "ptb");
	if (!fw) {
		dev_err(&bcm4377->pdev->dev, "failed to load PTB data");
		return -ENOENT;
	}

	ret = bcm4377->hw->send_ptb(bcm4377, fw);
	release_firmware(fw);
	if (ret)
		return ret;

	return bcm4377_check_bdaddr(bcm4377);
}

static int bcm4377_hci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct bcm4377_data *bcm4377 = hci_get_drvdata(hdev);
	struct bcm4377_transfer_ring *ring;
	int ret;

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		ring = &bcm4377->hci_h2d_ring;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		ring = &bcm4377->acl_h2d_ring;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		ring = &bcm4377->sco_h2d_ring;
		break;

	default:
		return -EILSEQ;
	}

	ret = bcm4377_enqueue(bcm4377, ring, skb->data, skb->len, false);
	if (ret < 0) {
		hdev->stat.err_tx++;
		return ret;
	}

	hdev->stat.byte_tx += skb->len;
	kfree_skb(skb);
	return ret;
}

static int bcm4377_hci_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct bcm4377_data *bcm4377 = hci_get_drvdata(hdev);
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync(hdev, 0xfc01, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		dev_err(&bcm4377->pdev->dev,
			"Change address command failed (%d)", err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}

static int bcm4377_alloc_transfer_ring(struct bcm4377_data *bcm4377,
				       struct bcm4377_transfer_ring *ring)
{
	size_t entry_size;

	spin_lock_init(&ring->lock);
	ring->payload_size = ALIGN(ring->payload_size, 4);
	ring->mapped_payload_size = ALIGN(ring->mapped_payload_size, 4);

	if (ring->payload_size > BCM4377_XFER_RING_MAX_INPLACE_PAYLOAD_SIZE)
		return -EINVAL;
	if (ring->n_entries > BCM4377_MAX_RING_SIZE)
		return -EINVAL;
	if (ring->virtual && ring->allow_wait)
		return -EINVAL;

	if (ring->d2h_buffers_only) {
		if (ring->virtual)
			return -EINVAL;
		if (ring->payload_size)
			return -EINVAL;
		if (!ring->mapped_payload_size)
			return -EINVAL;
	}
	if (ring->virtual)
		return 0;

	entry_size =
		ring->payload_size + sizeof(struct bcm4377_xfer_ring_entry);
	ring->ring = dmam_alloc_coherent(&bcm4377->pdev->dev,
					 ring->n_entries * entry_size,
					 &ring->ring_dma, GFP_KERNEL);
	if (!ring->ring)
		return -ENOMEM;

	if (ring->allow_wait) {
		ring->events = devm_kcalloc(&bcm4377->pdev->dev,
					    ring->n_entries,
					    sizeof(*ring->events), GFP_KERNEL);
		if (!ring->events)
			return -ENOMEM;
	}

	if (ring->mapped_payload_size) {
		ring->payloads = dmam_alloc_coherent(
			&bcm4377->pdev->dev,
			ring->n_entries * ring->mapped_payload_size,
			&ring->payloads_dma, GFP_KERNEL);
		if (!ring->payloads)
			return -ENOMEM;
	}

	return 0;
}

static int bcm4377_alloc_completion_ring(struct bcm4377_data *bcm4377,
					 struct bcm4377_completion_ring *ring)
{
	size_t entry_size;

	ring->payload_size = ALIGN(ring->payload_size, 4);
	if (ring->payload_size > BCM4377_XFER_RING_MAX_INPLACE_PAYLOAD_SIZE)
		return -EINVAL;
	if (ring->n_entries > BCM4377_MAX_RING_SIZE)
		return -EINVAL;

	entry_size = ring->payload_size +
		     sizeof(struct bcm4377_completion_ring_entry);

	ring->ring = dmam_alloc_coherent(&bcm4377->pdev->dev,
					 ring->n_entries * entry_size,
					 &ring->ring_dma, GFP_KERNEL);
	if (!ring->ring)
		return -ENOMEM;
	return 0;
}

static int bcm4377_init_context(struct bcm4377_data *bcm4377)
{
	struct device *dev = &bcm4377->pdev->dev;
	dma_addr_t peripheral_info_dma;

	bcm4377->ctx = dmam_alloc_coherent(dev, sizeof(*bcm4377->ctx),
					   &bcm4377->ctx_dma, GFP_KERNEL);
	if (!bcm4377->ctx)
		return -ENOMEM;
	memset(bcm4377->ctx, 0, sizeof(*bcm4377->ctx));

	bcm4377->ring_state =
		dmam_alloc_coherent(dev, sizeof(*bcm4377->ring_state),
				    &bcm4377->ring_state_dma, GFP_KERNEL);
	if (!bcm4377->ring_state)
		return -ENOMEM;
	memset(bcm4377->ring_state, 0, sizeof(*bcm4377->ring_state));

	bcm4377->ctx->version = cpu_to_le16(1);
	bcm4377->ctx->size = cpu_to_le16(sizeof(*bcm4377->ctx));
	bcm4377->ctx->enabled_caps = cpu_to_le32(2);

	/*
	 * The BT device will write 0x20 bytes of data to this buffer but
	 * the exact contents are unknown. It only needs to exist for BT
	 * to work such that we can just allocate and then ignore it.
	 */
	if (!dmam_alloc_coherent(&bcm4377->pdev->dev, 0x20,
				 &peripheral_info_dma, GFP_KERNEL))
		return -ENOMEM;
	bcm4377->ctx->peripheral_info_addr = cpu_to_le64(peripheral_info_dma);

	bcm4377->ctx->xfer_ring_heads_addr = cpu_to_le64(
		bcm4377->ring_state_dma +
		offsetof(struct bcm4377_ring_state, xfer_ring_head));
	bcm4377->ctx->xfer_ring_tails_addr = cpu_to_le64(
		bcm4377->ring_state_dma +
		offsetof(struct bcm4377_ring_state, xfer_ring_tail));
	bcm4377->ctx->completion_ring_heads_addr = cpu_to_le64(
		bcm4377->ring_state_dma +
		offsetof(struct bcm4377_ring_state, completion_ring_head));
	bcm4377->ctx->completion_ring_tails_addr = cpu_to_le64(
		bcm4377->ring_state_dma +
		offsetof(struct bcm4377_ring_state, completion_ring_tail));

	bcm4377->ctx->n_completion_rings =
		cpu_to_le16(BCM4377_N_COMPLETION_RINGS);
	bcm4377->ctx->n_xfer_rings = cpu_to_le16(BCM4377_N_TRANSFER_RINGS);

	bcm4377->ctx->control_completion_ring_addr =
		cpu_to_le64(bcm4377->control_ack_ring.ring_dma);
	bcm4377->ctx->control_completion_ring_n_entries =
		cpu_to_le16(bcm4377->control_ack_ring.n_entries);
	bcm4377->ctx->control_completion_ring_doorbell = cpu_to_le16(0xffff);
	bcm4377->ctx->control_completion_ring_msi = 0;
	bcm4377->ctx->control_completion_ring_header_size = 0;
	bcm4377->ctx->control_completion_ring_footer_size = 0;

	bcm4377->ctx->control_xfer_ring_addr =
		cpu_to_le64(bcm4377->control_h2d_ring.ring_dma);
	bcm4377->ctx->control_xfer_ring_n_entries =
		cpu_to_le16(bcm4377->control_h2d_ring.n_entries);
	bcm4377->ctx->control_xfer_ring_doorbell =
		cpu_to_le16(bcm4377->control_h2d_ring.doorbell);
	bcm4377->ctx->control_xfer_ring_msi = 0;
	bcm4377->ctx->control_xfer_ring_header_size = 0;
	bcm4377->ctx->control_xfer_ring_footer_size =
		bcm4377->control_h2d_ring.payload_size / 4;

	dev_dbg(&bcm4377->pdev->dev, "context initialized at IOVA %pad",
		&bcm4377->ctx_dma);

	return 0;
}

static int bcm4377_prepare_rings(struct bcm4377_data *bcm4377)
{
	int ret;

	/*
	 * Even though many of these settings appear to be configurable
	 * when sending the "create ring" messages most of these are
	 * actually hardcoded in some (and quite possibly all) firmware versions
	 * and changing them on the host has no effect.
	 * Specifically, this applies to at least the doorbells, the transfer
	 * and completion ring ids and their mapping (e.g. both HCI and ACL
	 * entries will always be queued in completion rings 1 and 2 no matter
	 * what we configure here).
	 */
	bcm4377->control_ack_ring.ring_id = BCM4377_ACK_RING_CONTROL;
	bcm4377->control_ack_ring.n_entries = 32;
	bcm4377->control_ack_ring.transfer_rings =
		BIT(BCM4377_XFER_RING_CONTROL);

	bcm4377->hci_acl_ack_ring.ring_id = BCM4377_ACK_RING_HCI_ACL;
	bcm4377->hci_acl_ack_ring.n_entries = 2 * BCM4377_RING_N_ENTRIES;
	bcm4377->hci_acl_ack_ring.transfer_rings =
		BIT(BCM4377_XFER_RING_HCI_H2D) | BIT(BCM4377_XFER_RING_ACL_H2D);
	bcm4377->hci_acl_ack_ring.delay = 1000;

	/*
	 * A payload size of MAX_EVENT_PAYLOAD_SIZE is enough here since large
	 * ACL packets will be transmitted inside buffers mapped via
	 * acl_d2h_ring anyway.
	 */
	bcm4377->hci_acl_event_ring.ring_id = BCM4377_EVENT_RING_HCI_ACL;
	bcm4377->hci_acl_event_ring.payload_size = MAX_EVENT_PAYLOAD_SIZE;
	bcm4377->hci_acl_event_ring.n_entries = 2 * BCM4377_RING_N_ENTRIES;
	bcm4377->hci_acl_event_ring.transfer_rings =
		BIT(BCM4377_XFER_RING_HCI_D2H) | BIT(BCM4377_XFER_RING_ACL_D2H);
	bcm4377->hci_acl_event_ring.delay = 1000;

	bcm4377->sco_ack_ring.ring_id = BCM4377_ACK_RING_SCO;
	bcm4377->sco_ack_ring.n_entries = BCM4377_RING_N_ENTRIES;
	bcm4377->sco_ack_ring.transfer_rings = BIT(BCM4377_XFER_RING_SCO_H2D);

	bcm4377->sco_event_ring.ring_id = BCM4377_EVENT_RING_SCO;
	bcm4377->sco_event_ring.payload_size = MAX_SCO_PAYLOAD_SIZE;
	bcm4377->sco_event_ring.n_entries = BCM4377_RING_N_ENTRIES;
	bcm4377->sco_event_ring.transfer_rings = BIT(BCM4377_XFER_RING_SCO_D2H);

	bcm4377->control_h2d_ring.ring_id = BCM4377_XFER_RING_CONTROL;
	bcm4377->control_h2d_ring.doorbell = BCM4377_DOORBELL_CONTROL;
	bcm4377->control_h2d_ring.payload_size = BCM4377_CONTROL_MSG_SIZE;
	bcm4377->control_h2d_ring.completion_ring = BCM4377_ACK_RING_CONTROL;
	bcm4377->control_h2d_ring.allow_wait = true;
	bcm4377->control_h2d_ring.n_entries = BCM4377_RING_N_ENTRIES;

	bcm4377->hci_h2d_ring.ring_id = BCM4377_XFER_RING_HCI_H2D;
	bcm4377->hci_h2d_ring.doorbell = BCM4377_DOORBELL_HCI_H2D;
	bcm4377->hci_h2d_ring.payload_size = MAX_EVENT_PAYLOAD_SIZE;
	bcm4377->hci_h2d_ring.completion_ring = BCM4377_ACK_RING_HCI_ACL;
	bcm4377->hci_h2d_ring.n_entries = BCM4377_RING_N_ENTRIES;

	bcm4377->hci_d2h_ring.ring_id = BCM4377_XFER_RING_HCI_D2H;
	bcm4377->hci_d2h_ring.doorbell = BCM4377_DOORBELL_HCI_D2H;
	bcm4377->hci_d2h_ring.completion_ring = BCM4377_EVENT_RING_HCI_ACL;
	bcm4377->hci_d2h_ring.virtual = true;
	bcm4377->hci_d2h_ring.n_entries = BCM4377_RING_N_ENTRIES;

	bcm4377->sco_h2d_ring.ring_id = BCM4377_XFER_RING_SCO_H2D;
	bcm4377->sco_h2d_ring.doorbell = BCM4377_DOORBELL_SCO;
	bcm4377->sco_h2d_ring.payload_size = MAX_SCO_PAYLOAD_SIZE;
	bcm4377->sco_h2d_ring.completion_ring = BCM4377_ACK_RING_SCO;
	bcm4377->sco_h2d_ring.sync = true;
	bcm4377->sco_h2d_ring.n_entries = BCM4377_RING_N_ENTRIES;

	bcm4377->sco_d2h_ring.ring_id = BCM4377_XFER_RING_SCO_D2H;
	bcm4377->sco_d2h_ring.doorbell = BCM4377_DOORBELL_SCO;
	bcm4377->sco_d2h_ring.completion_ring = BCM4377_EVENT_RING_SCO;
	bcm4377->sco_d2h_ring.virtual = true;
	bcm4377->sco_d2h_ring.sync = true;
	bcm4377->sco_d2h_ring.n_entries = BCM4377_RING_N_ENTRIES;

	/*
	 * This ring has to use mapped_payload_size because the largest ACL
	 * packet doesn't fit inside the largest possible footer
	 */
	bcm4377->acl_h2d_ring.ring_id = BCM4377_XFER_RING_ACL_H2D;
	bcm4377->acl_h2d_ring.doorbell = BCM4377_DOORBELL_ACL_H2D;
	bcm4377->acl_h2d_ring.mapped_payload_size = MAX_ACL_PAYLOAD_SIZE;
	bcm4377->acl_h2d_ring.completion_ring = BCM4377_ACK_RING_HCI_ACL;
	bcm4377->acl_h2d_ring.n_entries = BCM4377_RING_N_ENTRIES;

	/*
	 * This ring only contains empty buffers to be used by incoming
	 * ACL packets that do not fit inside the footer of hci_acl_event_ring
	 */
	bcm4377->acl_d2h_ring.ring_id = BCM4377_XFER_RING_ACL_D2H;
	bcm4377->acl_d2h_ring.doorbell = BCM4377_DOORBELL_ACL_D2H;
	bcm4377->acl_d2h_ring.completion_ring = BCM4377_EVENT_RING_HCI_ACL;
	bcm4377->acl_d2h_ring.d2h_buffers_only = true;
	bcm4377->acl_d2h_ring.mapped_payload_size = MAX_ACL_PAYLOAD_SIZE;
	bcm4377->acl_d2h_ring.n_entries = BCM4377_RING_N_ENTRIES;

	/*
	 * no need for any cleanup since this is only called from _probe
	 * and only devres-managed allocations are used
	 */
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->control_h2d_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->hci_h2d_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->hci_d2h_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->sco_h2d_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->sco_d2h_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->acl_h2d_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_transfer_ring(bcm4377, &bcm4377->acl_d2h_ring);
	if (ret)
		return ret;

	ret = bcm4377_alloc_completion_ring(bcm4377,
					    &bcm4377->control_ack_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_completion_ring(bcm4377,
					    &bcm4377->hci_acl_ack_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_completion_ring(bcm4377,
					    &bcm4377->hci_acl_event_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_completion_ring(bcm4377, &bcm4377->sco_ack_ring);
	if (ret)
		return ret;
	ret = bcm4377_alloc_completion_ring(bcm4377, &bcm4377->sco_event_ring);
	if (ret)
		return ret;

	dev_dbg(&bcm4377->pdev->dev, "all rings allocated and prepared\n");

	return 0;
}

static int bcm4377_boot(struct bcm4377_data *bcm4377)
{
	const struct firmware *fw;
	void *bfr;
	dma_addr_t fw_dma;
	int ret = 0;
	u32 bootstage, rti_status;

	bootstage = ioread32(bcm4377->bar2 + BCM4377_BAR2_BOOTSTAGE);
	rti_status = ioread32(bcm4377->bar2 + BCM4377_BAR2_RTI_STATUS);

	if (bootstage != 0) {
		dev_err(&bcm4377->pdev->dev, "bootstage is %d and not 0\n",
			bootstage);
		return -EINVAL;
	}

	if (rti_status != 0) {
		dev_err(&bcm4377->pdev->dev, "RTI status is %d and not 0\n",
			rti_status);
		return -EINVAL;
	}

	fw = bcm4377_request_blob(bcm4377, "bin");
	if (!fw) {
		dev_err(&bcm4377->pdev->dev, "Failed to load firmware\n");
		return -ENOENT;
	}

	bfr = dma_alloc_coherent(&bcm4377->pdev->dev, fw->size, &fw_dma,
				 GFP_KERNEL);
	if (!bfr) {
		ret = -ENOMEM;
		goto out_release_fw;
	}

	memcpy(bfr, fw->data, fw->size);

	iowrite32(0, bcm4377->bar0 + BCM4377_BAR0_HOST_WINDOW_LO);
	iowrite32(0, bcm4377->bar0 + BCM4377_BAR0_HOST_WINDOW_HI);
	iowrite32(BCM4377_DMA_MASK,
		  bcm4377->bar0 + BCM4377_BAR0_HOST_WINDOW_SIZE);

	iowrite32(lower_32_bits(fw_dma), bcm4377->bar2 + BCM4377_BAR2_FW_LO);
	iowrite32(upper_32_bits(fw_dma), bcm4377->bar2 + BCM4377_BAR2_FW_HI);
	iowrite32(fw->size, bcm4377->bar2 + BCM4377_BAR2_FW_SIZE);
	iowrite32(0, bcm4377->bar0 + BCM4377_BAR0_FW_DOORBELL);

	dev_dbg(&bcm4377->pdev->dev, "waiting for firmware to boot\n");

	ret = wait_for_completion_interruptible_timeout(&bcm4377->event,
							BCM4377_TIMEOUT);
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto out_dma_free;
	} else if (ret < 0) {
		goto out_dma_free;
	}

	if (bcm4377->bootstage != 2) {
		dev_err(&bcm4377->pdev->dev, "boostage %d != 2\n",
			bcm4377->bootstage);
		ret = -ENXIO;
		goto out_dma_free;
	}

	dev_dbg(&bcm4377->pdev->dev, "firmware has booted (stage = %x)\n",
		bcm4377->bootstage);
	ret = 0;

out_dma_free:
	dma_free_coherent(&bcm4377->pdev->dev, fw->size, bfr, fw_dma);
out_release_fw:
	release_firmware(fw);
	return ret;
}

static int bcm4377_setup_rti(struct bcm4377_data *bcm4377)
{
	int ret;

	dev_dbg(&bcm4377->pdev->dev, "starting RTI\n");
	iowrite32(1, bcm4377->bar0 + BCM4377_BAR0_RTI_CONTROL);

	ret = wait_for_completion_interruptible_timeout(&bcm4377->event,
							BCM4377_TIMEOUT);
	if (ret == 0) {
		dev_err(&bcm4377->pdev->dev,
			"timed out while waiting for RTI to transition to state 1");
		return -ETIMEDOUT;
	} else if (ret < 0) {
		return ret;
	}

	if (bcm4377->rti_status != 1) {
		dev_err(&bcm4377->pdev->dev, "RTI did not ack state 1 (%d)\n",
			bcm4377->rti_status);
		return -ENODEV;
	}
	dev_dbg(&bcm4377->pdev->dev, "RTI is in state 1\n");

	/* allow access to the entire IOVA space again */
	iowrite32(0, bcm4377->bar2 + BCM4377_BAR2_RTI_WINDOW_LO);
	iowrite32(0, bcm4377->bar2 + BCM4377_BAR2_RTI_WINDOW_HI);
	iowrite32(BCM4377_DMA_MASK,
		  bcm4377->bar2 + BCM4377_BAR2_RTI_WINDOW_SIZE);

	/* setup "Converged IPC" context */
	iowrite32(lower_32_bits(bcm4377->ctx_dma),
		  bcm4377->bar2 + BCM4377_BAR2_CONTEXT_ADDR_LO);
	iowrite32(upper_32_bits(bcm4377->ctx_dma),
		  bcm4377->bar2 + BCM4377_BAR2_CONTEXT_ADDR_HI);
	iowrite32(2, bcm4377->bar0 + BCM4377_BAR0_RTI_CONTROL);

	ret = wait_for_completion_interruptible_timeout(&bcm4377->event,
							BCM4377_TIMEOUT);
	if (ret == 0) {
		dev_err(&bcm4377->pdev->dev,
			"timed out while waiting for RTI to transition to state 2");
		return -ETIMEDOUT;
	} else if (ret < 0) {
		return ret;
	}

	if (bcm4377->rti_status != 2) {
		dev_err(&bcm4377->pdev->dev, "RTI did not ack state 2 (%d)\n",
			bcm4377->rti_status);
		return -ENODEV;
	}

	dev_dbg(&bcm4377->pdev->dev,
		"RTI is in state 2; control ring is ready\n");
	bcm4377->control_ack_ring.enabled = true;

	return 0;
}

static int bcm4377_parse_otp_board_params(struct bcm4377_data *bcm4377,
					  char tag, const char *val, size_t len)
{
	if (tag != 'V')
		return 0;
	if (len >= sizeof(bcm4377->vendor))
		return -EINVAL;

	strscpy(bcm4377->vendor, val, len + 1);
	return 0;
}

static int bcm4377_parse_otp_chip_params(struct bcm4377_data *bcm4377, char tag,
					 const char *val, size_t len)
{
	size_t idx = 0;

	if (tag != 's')
		return 0;
	if (len >= sizeof(bcm4377->stepping))
		return -EINVAL;

	while (len != 0) {
		bcm4377->stepping[idx] = tolower(val[idx]);
		if (val[idx] == '\0')
			return 0;

		idx++;
		len--;
	}

	bcm4377->stepping[idx] = '\0';
	return 0;
}

static int bcm4377_parse_otp_str(struct bcm4377_data *bcm4377, const u8 *str,
				 enum bcm4377_otp_params_type type)
{
	const char *p;
	int ret;

	p = skip_spaces(str);
	while (*p) {
		char tag = *p++;
		const char *end;
		size_t len;

		if (*p++ != '=') /* implicit NUL check */
			return -EINVAL;

		/* *p might be NUL here, if so end == p and len == 0 */
		end = strchrnul(p, ' ');
		len = end - p;

		/* leave 1 byte for NUL in destination string */
		if (len > (BCM4377_OTP_MAX_PARAM_LEN - 1))
			return -EINVAL;

		switch (type) {
		case BCM4377_OTP_BOARD_PARAMS:
			ret = bcm4377_parse_otp_board_params(bcm4377, tag, p,
							     len);
			break;
		case BCM4377_OTP_CHIP_PARAMS:
			ret = bcm4377_parse_otp_chip_params(bcm4377, tag, p,
							    len);
			break;
		default:
			ret = -EINVAL;
			break;
		}

		if (ret)
			return ret;

		/* Skip to next arg, if any */
		p = skip_spaces(end);
	}

	return 0;
}

static int bcm4377_parse_otp_sys_vendor(struct bcm4377_data *bcm4377, u8 *otp,
					size_t size)
{
	int idx = 4;
	const char *chip_params;
	const char *board_params;
	int ret;

	/* 4-byte header and two empty strings */
	if (size < 6)
		return -EINVAL;

	if (get_unaligned_le32(otp) != BCM4377_OTP_VENDOR_HDR)
		return -EINVAL;

	chip_params = &otp[idx];

	/* Skip first string, including terminator */
	idx += strnlen(chip_params, size - idx) + 1;
	if (idx >= size)
		return -EINVAL;

	board_params = &otp[idx];

	/* Skip to terminator of second string */
	idx += strnlen(board_params, size - idx);
	if (idx >= size)
		return -EINVAL;

	/* At this point both strings are guaranteed NUL-terminated */
	dev_dbg(&bcm4377->pdev->dev,
		"OTP: chip_params='%s' board_params='%s'\n", chip_params,
		board_params);

	ret = bcm4377_parse_otp_str(bcm4377, chip_params,
				    BCM4377_OTP_CHIP_PARAMS);
	if (ret)
		return ret;

	ret = bcm4377_parse_otp_str(bcm4377, board_params,
				    BCM4377_OTP_BOARD_PARAMS);
	if (ret)
		return ret;

	if (!bcm4377->stepping[0] || !bcm4377->vendor[0])
		return -EINVAL;

	dev_dbg(&bcm4377->pdev->dev, "OTP: stepping=%s, vendor=%s\n",
		bcm4377->stepping, bcm4377->vendor);
	return 0;
}

static int bcm4377_parse_otp(struct bcm4377_data *bcm4377)
{
	u8 *otp;
	int i;
	int ret = -ENOENT;

	otp = kzalloc(BCM4377_OTP_SIZE, GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	for (i = 0; i < BCM4377_OTP_SIZE; ++i)
		otp[i] = ioread8(bcm4377->bar0 + bcm4377->hw->otp_offset + i);

	i = 0;
	while (i < (BCM4377_OTP_SIZE - 1)) {
		u8 type = otp[i];
		u8 length = otp[i + 1];

		if (type == 0)
			break;

		if ((i + 2 + length) > BCM4377_OTP_SIZE)
			break;

		switch (type) {
		case BCM4377_OTP_SYS_VENDOR:
			dev_dbg(&bcm4377->pdev->dev,
				"OTP @ 0x%x (%d): SYS_VENDOR", i, length);
			ret = bcm4377_parse_otp_sys_vendor(bcm4377, &otp[i + 2],
							   length);
			break;
		case BCM4377_OTP_CIS:
			dev_dbg(&bcm4377->pdev->dev, "OTP @ 0x%x (%d): CIS", i,
				length);
			break;
		default:
			dev_dbg(&bcm4377->pdev->dev, "OTP @ 0x%x (%d): unknown",
				i, length);
			break;
		}

		i += 2 + length;
	}

	kfree(otp);
	return ret;
}

static int bcm4377_init_cfg(struct bcm4377_data *bcm4377)
{
	int ret;
	u32 ctrl;

	ret = pci_write_config_dword(bcm4377->pdev,
				     BCM4377_PCIECFG_BAR0_WINDOW1,
				     bcm4377->hw->bar0_window1);
	if (ret)
		return ret;

	ret = pci_write_config_dword(bcm4377->pdev,
				     BCM4377_PCIECFG_BAR0_WINDOW2,
				     bcm4377->hw->bar0_window2);
	if (ret)
		return ret;

	ret = pci_write_config_dword(
		bcm4377->pdev, BCM4377_PCIECFG_BAR0_CORE2_WINDOW1,
		BCM4377_PCIECFG_BAR0_CORE2_WINDOW1_DEFAULT);
	if (ret)
		return ret;

	if (bcm4377->hw->has_bar0_core2_window2) {
		ret = pci_write_config_dword(bcm4377->pdev,
					     BCM4377_PCIECFG_BAR0_CORE2_WINDOW2,
					     bcm4377->hw->bar0_core2_window2);
		if (ret)
			return ret;
	}

	ret = pci_write_config_dword(bcm4377->pdev, BCM4377_PCIECFG_BAR2_WINDOW,
				     BCM4377_PCIECFG_BAR2_WINDOW_DEFAULT);
	if (ret)
		return ret;

	ret = pci_read_config_dword(bcm4377->pdev,
				    BCM4377_PCIECFG_SUBSYSTEM_CTRL, &ctrl);
	if (ret)
		return ret;

	if (bcm4377->hw->clear_pciecfg_subsystem_ctrl_bit19)
		ctrl &= ~BIT(19);
	ctrl |= BIT(16);

	return pci_write_config_dword(bcm4377->pdev,
				      BCM4377_PCIECFG_SUBSYSTEM_CTRL, ctrl);
}

static int bcm4377_probe_dmi(struct bcm4377_data *bcm4377)
{
	const struct dmi_system_id *board_type_dmi_id;

	board_type_dmi_id = dmi_first_match(bcm4377_dmi_board_table);
	if (board_type_dmi_id && board_type_dmi_id->driver_data) {
		bcm4377->board_type = board_type_dmi_id->driver_data;
		dev_dbg(&bcm4377->pdev->dev,
			"found board type via DMI match: %s\n",
			bcm4377->board_type);
	}

	return 0;
}

static int bcm4377_probe_of(struct bcm4377_data *bcm4377)
{
	struct device_node *np = bcm4377->pdev->dev.of_node;
	int ret;

	if (!np)
		return 0;

	ret = of_property_read_string(np, "brcm,board-type",
				      &bcm4377->board_type);
	if (ret) {
		dev_err(&bcm4377->pdev->dev, "no brcm,board-type property\n");
		return ret;
	}

	bcm4377->taurus_beamforming_cal_blob =
		of_get_property(np, "brcm,taurus-bf-cal-blob",
				&bcm4377->taurus_beamforming_cal_size);
	if (!bcm4377->taurus_beamforming_cal_blob) {
		dev_err(&bcm4377->pdev->dev,
			"no brcm,taurus-bf-cal-blob property\n");
		return -ENOENT;
	}
	bcm4377->taurus_cal_blob = of_get_property(np, "brcm,taurus-cal-blob",
						   &bcm4377->taurus_cal_size);
	if (!bcm4377->taurus_cal_blob) {
		dev_err(&bcm4377->pdev->dev,
			"no brcm,taurus-cal-blob property\n");
		return -ENOENT;
	}

	return 0;
}

static void bcm4377_disable_aspm(struct bcm4377_data *bcm4377)
{
	pci_disable_link_state(bcm4377->pdev,
			       PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);

	/*
	 * pci_disable_link_state can fail if either CONFIG_PCIEASPM is disabled
	 * or if the BIOS hasn't handed over control to us. We must *always*
	 * disable ASPM for this device due to hardware errata though.
	 */
	pcie_capability_clear_word(bcm4377->pdev, PCI_EXP_LNKCTL,
				   PCI_EXP_LNKCTL_ASPMC);
}

static void bcm4377_pci_free_irq_vectors(void *data)
{
	pci_free_irq_vectors(data);
}

static void bcm4377_hci_free_dev(void *data)
{
	hci_free_dev(data);
}

static void bcm4377_hci_unregister_dev(void *data)
{
	hci_unregister_dev(data);
}

static int bcm4377_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct bcm4377_data *bcm4377;
	struct hci_dev *hdev;
	int ret, irq;

	ret = dma_set_mask_and_coherent(&pdev->dev, BCM4377_DMA_MASK);
	if (ret)
		return ret;

	bcm4377 = devm_kzalloc(&pdev->dev, sizeof(*bcm4377), GFP_KERNEL);
	if (!bcm4377)
		return -ENOMEM;

	bcm4377->pdev = pdev;
	bcm4377->hw = &bcm4377_hw_variants[id->driver_data];
	init_completion(&bcm4377->event);

	ret = bcm4377_prepare_rings(bcm4377);
	if (ret)
		return ret;

	ret = bcm4377_init_context(bcm4377);
	if (ret)
		return ret;

	ret = bcm4377_probe_dmi(bcm4377);
	if (ret)
		return ret;
	ret = bcm4377_probe_of(bcm4377);
	if (ret)
		return ret;
	if (!bcm4377->board_type) {
		dev_err(&pdev->dev, "unable to determine board type\n");
		return -ENODEV;
	}

	if (bcm4377->hw->disable_aspm)
		bcm4377_disable_aspm(bcm4377);

	ret = pci_reset_function_locked(pdev);
	if (ret)
		dev_warn(
			&pdev->dev,
			"function level reset failed with %d; trying to continue anyway\n",
			ret);

	/*
	 * If this number is too low and we try to access any BAR too
	 * early the device will crash. Experiments have shown that
	 * approximately 50 msec is the minimum amount we have to wait.
	 * Let's double that to be safe.
	 */
	msleep(100);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	pci_set_master(pdev);

	ret = bcm4377_init_cfg(bcm4377);
	if (ret)
		return ret;

	bcm4377->bar0 = pcim_iomap(pdev, 0, 0);
	if (!bcm4377->bar0)
		return -EBUSY;
	bcm4377->bar2 = pcim_iomap(pdev, 2, 0);
	if (!bcm4377->bar2)
		return -EBUSY;

	ret = bcm4377_parse_otp(bcm4377);
	if (ret) {
		dev_err(&pdev->dev, "Reading OTP failed with %d\n", ret);
		return ret;
	}

	/*
	 * Legacy interrupts result in an IRQ storm because we don't know where
	 * the interrupt mask and status registers for these chips are.
	 * MSIs are acked automatically instead.
	 */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0)
		return -ENODEV;
	ret = devm_add_action_or_reset(&pdev->dev, bcm4377_pci_free_irq_vectors,
				       pdev);
	if (ret)
		return ret;

	irq = pci_irq_vector(pdev, 0);
	if (irq <= 0)
		return -ENODEV;

	ret = devm_request_irq(&pdev->dev, irq, bcm4377_irq, 0, "bcm4377",
			       bcm4377);
	if (ret)
		return ret;

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;
	ret = devm_add_action_or_reset(&pdev->dev, bcm4377_hci_free_dev, hdev);
	if (ret)
		return ret;

	bcm4377->hdev = hdev;

	hdev->bus = HCI_PCI;
	hdev->open = bcm4377_hci_open;
	hdev->close = bcm4377_hci_close;
	hdev->send = bcm4377_hci_send_frame;
	hdev->set_bdaddr = bcm4377_hci_set_bdaddr;
	hdev->setup = bcm4377_hci_setup;

	if (bcm4377->hw->broken_mws_transport_config)
		set_bit(HCI_QUIRK_BROKEN_MWS_TRANSPORT_CONFIG, &hdev->quirks);
	if (bcm4377->hw->broken_ext_scan)
		set_bit(HCI_QUIRK_BROKEN_EXT_SCAN, &hdev->quirks);
	if (bcm4377->hw->broken_le_coded)
		set_bit(HCI_QUIRK_BROKEN_LE_CODED, &hdev->quirks);

	pci_set_drvdata(pdev, bcm4377);
	hci_set_drvdata(hdev, bcm4377);
	SET_HCIDEV_DEV(hdev, &pdev->dev);

	ret = bcm4377_boot(bcm4377);
	if (ret)
		return ret;

	ret = bcm4377_setup_rti(bcm4377);
	if (ret)
		return ret;

	ret = hci_register_dev(hdev);
	if (ret)
		return ret;
	return devm_add_action_or_reset(&pdev->dev, bcm4377_hci_unregister_dev,
					hdev);
}

static int bcm4377_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct bcm4377_data *bcm4377 = pci_get_drvdata(pdev);
	int ret;

	ret = hci_suspend_dev(bcm4377->hdev);
	if (ret)
		return ret;

	iowrite32(BCM4377_BAR0_SLEEP_CONTROL_QUIESCE,
		  bcm4377->bar0 + BCM4377_BAR0_SLEEP_CONTROL);

	return 0;
}

static int bcm4377_resume(struct pci_dev *pdev)
{
	struct bcm4377_data *bcm4377 = pci_get_drvdata(pdev);

	iowrite32(BCM4377_BAR0_SLEEP_CONTROL_UNQUIESCE,
		  bcm4377->bar0 + BCM4377_BAR0_SLEEP_CONTROL);

	return hci_resume_dev(bcm4377->hdev);
}

static const struct dmi_system_id bcm4377_dmi_board_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookAir9,1"),
		},
		.driver_data = "apple,formosa",
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro15,4"),
		},
		.driver_data = "apple,formosa",
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro16,3"),
		},
		.driver_data = "apple,formosa",
	},
	{}
};

static const struct bcm4377_hw bcm4377_hw_variants[] = {
	[BCM4377] = {
		.id = 0x4377,
		.otp_offset = 0x4120,
		.bar0_window1 = 0x1800b000,
		.bar0_window2 = 0x1810c000,
		.disable_aspm = true,
		.broken_ext_scan = true,
		.send_ptb = bcm4377_send_ptb,
	},

	[BCM4378] = {
		.id = 0x4378,
		.otp_offset = 0x4120,
		.bar0_window1 = 0x18002000,
		.bar0_window2 = 0x1810a000,
		.bar0_core2_window2 = 0x18107000,
		.has_bar0_core2_window2 = true,
		.broken_mws_transport_config = true,
		.broken_le_coded = true,
		.send_calibration = bcm4378_send_calibration,
		.send_ptb = bcm4378_send_ptb,
	},

	[BCM4387] = {
		.id = 0x4387,
		.otp_offset = 0x413c,
		.bar0_window1 = 0x18002000,
		.bar0_window2 = 0x18109000,
		.bar0_core2_window2 = 0x18106000,
		.has_bar0_core2_window2 = true,
		.clear_pciecfg_subsystem_ctrl_bit19 = true,
		.broken_mws_transport_config = true,
		.broken_le_coded = true,
		.send_calibration = bcm4387_send_calibration,
		.send_ptb = bcm4378_send_ptb,
	},
};

#define BCM4377_DEVID_ENTRY(id)                                             \
	{                                                                   \
		PCI_VENDOR_ID_BROADCOM, BCM##id##_DEVICE_ID, PCI_ANY_ID,    \
			PCI_ANY_ID, PCI_CLASS_NETWORK_OTHER << 8, 0xffff00, \
			BCM##id                                             \
	}

static const struct pci_device_id bcm4377_devid_table[] = {
	BCM4377_DEVID_ENTRY(4377),
	BCM4377_DEVID_ENTRY(4378),
	BCM4377_DEVID_ENTRY(4387),
	{},
};
MODULE_DEVICE_TABLE(pci, bcm4377_devid_table);

static struct pci_driver bcm4377_pci_driver = {
	.name = "hci_bcm4377",
	.id_table = bcm4377_devid_table,
	.probe = bcm4377_probe,
	.suspend = bcm4377_suspend,
	.resume = bcm4377_resume,
};
module_pci_driver(bcm4377_pci_driver);

MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_DESCRIPTION("Bluetooth support for Broadcom 4377/4378/4387 devices");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_FIRMWARE("brcm/brcmbt4377*.bin");
MODULE_FIRMWARE("brcm/brcmbt4377*.ptb");
MODULE_FIRMWARE("brcm/brcmbt4378*.bin");
MODULE_FIRMWARE("brcm/brcmbt4378*.ptb");
MODULE_FIRMWARE("brcm/brcmbt4387*.bin");
MODULE_FIRMWARE("brcm/brcmbt4387*.ptb");
