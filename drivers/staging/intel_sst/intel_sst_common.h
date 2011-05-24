#ifndef __INTEL_SST_COMMON_H__
#define __INTEL_SST_COMMON_H__
/*
 *  intel_sst_common.h - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2008-10 Intel Corporation
 *  Authors:	Vinod Koul <vinod.koul@intel.com>
 *		Harsha Priya <priya.harsha@intel.com>
 *		Dharageswari R <dharageswari.r@intel.com>
 *		KP Jeeja <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  Common private declarations for SST
 */

#define SST_DRIVER_VERSION "1.2.17"
#define SST_VERSION_NUM 0x1217

/* driver names */
#define SST_DRV_NAME "intel_sst_driver"
#define SST_MRST_PCI_ID 0x080A
#define SST_MFLD_PCI_ID 0x082F
#define PCI_ID_LENGTH 4
#define SST_SUSPEND_DELAY 2000
#define FW_CONTEXT_MEM (64*1024)

enum sst_states {
	SST_FW_LOADED = 1,
	SST_FW_RUNNING,
	SST_UN_INIT,
	SST_ERROR,
	SST_SUSPENDED
};

#define MAX_ACTIVE_STREAM	3
#define MAX_ENC_STREAM		1
#define MAX_AM_HANDLES		1
#define ALLOC_TIMEOUT		5000
/* SST numbers */
#define SST_BLOCK_TIMEOUT	5000
#define TARGET_DEV_BLOCK_TIMEOUT	5000

#define BLOCK_UNINIT		-1
#define RX_TIMESLOT_UNINIT	-1

/* SST register map */
#define SST_CSR			0x00
#define SST_PISR		0x08
#define SST_PIMR		0x10
#define SST_ISRX		0x18
#define SST_IMRX		0x28
#define SST_IPCX		0x38 /* IPC IA-SST */
#define SST_IPCD		0x40 /* IPC SST-IA */
#define SST_ISRD		0x20 /* dummy register for shim workaround */
#define SST_SHIM_SIZE		0X44

#define SPI_MODE_ENABLE_BASE_ADDR 0xffae4000
#define FW_SIGNATURE_SIZE	4

/* PMIC and SST hardware states */
enum sst_mad_states {
	SND_MAD_UN_INIT = 0,
	SND_MAD_INIT_DONE,
};

/* stream states */
enum sst_stream_states {
	STREAM_UN_INIT	= 0,	/* Freed/Not used stream */
	STREAM_RUNNING	= 1,	/* Running */
	STREAM_PAUSED	= 2,	/* Paused stream */
	STREAM_DECODE	= 3,	/* stream is in decoding only state */
	STREAM_INIT	= 4,	/* stream init, waiting for data */
};


enum sst_ram_type {
	SST_IRAM	= 1,
	SST_DRAM	= 2,
};
/* SST shim registers to structure mapping  */
union config_status_reg {
	struct {
		u32 mfld_strb:1;
		u32 sst_reset:1;
		u32 hw_rsvd:3;
		u32 sst_clk:2;
		u32 bypass:3;
		u32 run_stall:1;
		u32 rsvd1:2;
		u32 strb_cntr_rst:1;
		u32 rsvd:18;
	} part;
	u32 full;
};

union interrupt_reg {
	struct {
		u32 done_interrupt:1;
		u32 busy_interrupt:1;
		u32 rsvd:30;
	} part;
	u32 full;
};

union sst_pisr_reg {
	struct {
		u32 pssp0:1;
		u32 pssp1:1;
		u32 rsvd0:3;
		u32 dmac:1;
		u32 rsvd1:26;
	} part;
	u32 full;
};

union sst_pimr_reg {
	struct {
		u32 ssp0:1;
		u32 ssp1:1;
		u32 rsvd0:3;
		u32 dmac:1;
		u32 rsvd1:10;
		u32 ssp0_sc:1;
		u32 ssp1_sc:1;
		u32 rsvd2:3;
		u32 dmac_sc:1;
		u32 rsvd3:10;
	} part;
	u32 full;
};


struct sst_stream_bufs {
	struct list_head	node;
	u32			size;
	const char		*addr;
	u32			data_copied;
	bool			in_use;
	u32			offset;
};

struct snd_sst_user_cap_list {
	unsigned int iov_index; /* index of iov */
	unsigned long iov_offset; /* offset in iov */
	unsigned long offset; /* offset in kmem */
	unsigned long size; /* size copied */
	struct list_head node;
};
/*
This structure is used to block a user/fw data call to another
fw/user call
*/
struct sst_block {
	bool	condition; /* condition for blocking check */
	int	ret_code; /* ret code when block is released */
	void	*data; /* data to be appsed for block if any */
	bool	on;
};

enum snd_sst_buf_type {
	SST_BUF_USER_STATIC = 1,
	SST_BUF_USER_DYNAMIC,
	SST_BUF_MMAP_STATIC,
	SST_BUF_MMAP_DYNAMIC,
};

enum snd_src {
	SST_DRV = 1,
	MAD_DRV = 2
};

/**
 * struct stream_info - structure that holds the stream information
 *
 * @status : stream current state
 * @prev : stream prev state
 * @codec : stream codec
 * @sst_id : stream id
 * @ops : stream operation pb/cp/drm...
 * @bufs: stream buffer list
 * @lock : stream mutex for protecting state
 * @pcm_lock : spinlock for pcm path only
 * @mmapped : is stream mmapped
 * @sg_index : current stream user buffer index
 * @cur_ptr : stream user buffer pointer
 * @buf_entry : current user buffer
 * @data_blk : stream block for data operations
 * @ctrl_blk : stream block for ctrl operations
 * @buf_type : stream user buffer type
 * @pcm_substream : PCM substream
 * @period_elapsed : PCM period elapsed callback
 * @sfreq : stream sampling freq
 * @decode_ibuf : Decoded i/p buffers pointer
 * @decode_obuf : Decoded o/p buffers pointer
 * @decode_isize : Decoded i/p buffers size
 * @decode_osize : Decoded o/p buffers size
 * @decode_ibuf_type : Decoded i/p buffer type
 * @decode_obuf_type : Decoded o/p buffer type
 * @idecode_alloc : Decode alloc index
 * @need_draining : stream set for drain
 * @str_type : stream type
 * @curr_bytes : current bytes decoded
 * @cumm_bytes : cummulative bytes decoded
 * @str_type : stream type
 * @src : stream source
 * @device : output device type (medfield only)
 * @pcm_slot : pcm slot value
 */
struct stream_info {
	unsigned int		status;
	unsigned int		prev;
	u8			codec;
	unsigned int		sst_id;
	unsigned int		ops;
	struct list_head	bufs;
	struct mutex		lock; /* mutex */
	spinlock_t          pcm_lock;
	bool			mmapped;
	unsigned int		sg_index; /*  current buf Index  */
	unsigned char __user 	*cur_ptr; /*  Current static bufs  */
	struct snd_sst_buf_entry __user *buf_entry;
	struct sst_block	data_blk; /* stream ops block */
	struct sst_block	ctrl_blk; /* stream control cmd block */
	enum snd_sst_buf_type   buf_type;
	void			*pcm_substream;
	void (*period_elapsed) (void *pcm_substream);
	unsigned int		sfreq;
	void			*decode_ibuf, *decode_obuf;
	unsigned int		decode_isize, decode_osize;
	u8 decode_ibuf_type, decode_obuf_type;
	unsigned int		idecode_alloc;
	unsigned int		need_draining;
	unsigned int		str_type;
	u32			curr_bytes;
	u32			cumm_bytes;
	u32			src;
	enum snd_sst_audio_device_type device;
	u8			pcm_slot;
};

/*
 * struct stream_alloc_bloc - this structure is used for blocking the user's
 * alloc calls to fw's response to alloc calls
 *
 * @sst_id : session id of blocked stream
 * @ops_block : ops block struture
 */
struct stream_alloc_block {
	int			sst_id; /* session id of blocked stream */
	struct sst_block	ops_block; /* ops block struture */
};

#define SST_FW_SIGN "$SST"
#define SST_FW_LIB_SIGN "$LIB"

/*
 * struct fw_header - FW file headers
 *
 * @signature : FW signature
 * @modules : # of modules
 * @file_format : version of header format
 * @reserved : reserved fields
 */
struct fw_header {
	unsigned char signature[FW_SIGNATURE_SIZE]; /* FW signature */
	u32 file_size; /* size of fw minus this header */
	u32 modules; /*  # of modules */
	u32 file_format; /* version of header format */
	u32 reserved[4];
};

struct fw_module_header {
	unsigned char signature[FW_SIGNATURE_SIZE]; /* module signature */
	u32 mod_size; /* size of module */
	u32 blocks; /* # of blocks */
	u32 type; /* codec type, pp lib */
	u32 entry_point;
};

struct dma_block_info {
	enum sst_ram_type	type;	/* IRAM/DRAM */
	u32			size;	/* Bytes */
	u32			ram_offset; /* Offset in I/DRAM */
	u32			rsvd;	/* Reserved field */
};

struct ioctl_pvt_data {
	int			str_id;
	int			pvt_id;
};

struct sst_ipc_msg_wq {
	union ipc_header	header;
	char mailbox[SST_MAILBOX_SIZE];
	struct work_struct	wq;
};

struct mad_ops_wq {
	int stream_id;
	enum sst_controls control_op;
	struct work_struct	wq;

};

#define SST_MMAP_PAGES	(640*1024 / PAGE_SIZE)
#define SST_MMAP_STEP	(40*1024 / PAGE_SIZE)

/***
 * struct intel_sst_drv - driver ops
 *
 * @pmic_state : pmic state
 * @pmic_vendor : pmic vendor detected
 * @sst_state : current sst device state
 * @pci_id : PCI device id loaded
 * @shim : SST shim pointer
 * @mailbox : SST mailbox pointer
 * @iram : SST IRAM pointer
 * @dram : SST DRAM pointer
 * @shim_phy_add : SST shim phy addr
 * @ipc_dispatch_list : ipc messages dispatched
 * @ipc_post_msg_wq : wq to post IPC messages context
 * @ipc_process_msg : wq to process msgs from FW context
 * @ipc_process_reply : wq to process reply from FW context
 * @ipc_post_msg : wq to post reply from FW context
 * @mad_ops : MAD driver operations registered
 * @mad_wq : MAD driver wq
 * @post_msg_wq : wq to post IPC messages
 * @process_msg_wq : wq to process msgs from FW
 * @process_reply_wq : wq to process reply from FW
 * @streams : sst stream contexts
 * @alloc_block : block structure for alloc
 * @tgt_dev_blk : block structure for target device
 * @fw_info_blk : block structure for fw info block
 * @vol_info_blk : block structure for vol info block
 * @mute_info_blk : block structure for mute info block
 * @hs_info_blk : block structure for hs info block
 * @list_lock : sst driver list lock (deprecated)
 * @list_spin_lock : sst driver spin lock block
 * @scard_ops : sst card ops
 * @pci : sst pci device struture
 * @active_streams : sst active streams
 * @sst_lock : sst device lock
 * @stream_lock : sst stream lock
 * @unique_id : sst unique id
 * @stream_cnt : total sst active stream count
 * @pb_streams : total active pb streams
 * @cp_streams : total active cp streams
 * @lpe_stalled : lpe stall status
 * @pmic_port_instance : active pmic port instance
 * @rx_time_slot_status : active rx slot
 * @lpaudio_start : lpaudio status
 * @audio_start : audio status
 * @devt_d : pointer to /dev/lpe node
 * @devt_c : pointer to /dev/lpe_ctrl node
 * @max_streams : max streams allowed
 */
struct intel_sst_drv {
	bool			pmic_state;
	int			pmic_vendor;
	int			sst_state;
	unsigned int		pci_id;
	void __iomem		*shim;
	void __iomem		*mailbox;
	void __iomem		*iram;
	void __iomem		*dram;
	unsigned int		shim_phy_add;
	struct list_head	ipc_dispatch_list;
	struct work_struct	ipc_post_msg_wq;
	struct sst_ipc_msg_wq	ipc_process_msg;
	struct sst_ipc_msg_wq	ipc_process_reply;
	struct sst_ipc_msg_wq	ipc_post_msg;
	struct mad_ops_wq	mad_ops;
	wait_queue_head_t	wait_queue;
	struct workqueue_struct *mad_wq;
	struct workqueue_struct *post_msg_wq;
	struct workqueue_struct *process_msg_wq;
	struct workqueue_struct *process_reply_wq;

	struct stream_info	streams[MAX_NUM_STREAMS];
	struct stream_alloc_block alloc_block[MAX_ACTIVE_STREAM];
	struct sst_block	tgt_dev_blk, fw_info_blk, ppp_params_blk,
				vol_info_blk, mute_info_blk, hs_info_blk;
	struct mutex		list_lock;/* mutex for IPC list locking */
	spinlock_t	list_spin_lock; /* mutex for IPC list locking */
	struct snd_pmic_ops	*scard_ops;
	struct pci_dev		*pci;
	int active_streams[MAX_NUM_STREAMS];
	void			*mmap_mem;
	struct mutex            sst_lock;
	struct mutex		stream_lock;
	unsigned int		mmap_len;
	unsigned int		unique_id;
	unsigned int		stream_cnt;	/* total streams */
	unsigned int		encoded_cnt;	/* enocded streams only */
	unsigned int		am_cnt;
	unsigned int		pb_streams;	/* pb streams active */
	unsigned int		cp_streams;	/* cp streams active */
	unsigned int		lpe_stalled; /* LPE is stalled or not */
	unsigned int		pmic_port_instance; /*pmic port instance*/
	int			rx_time_slot_status;
	unsigned int		lpaudio_start;
		/* 1 - LPA stream(MP3 pb) in progress*/
	unsigned int		audio_start;
	dev_t			devt_d, devt_c;
	unsigned int		max_streams;
	unsigned int		*fw_cntx;
	unsigned int		fw_cntx_size;
};

extern struct intel_sst_drv *sst_drv_ctx;

#define CHIP_REV_REG 0xff108000
#define CHIP_REV_ADDR 0x78

/* misc definitions */
#define FW_DWNL_ID 0xFF
#define LOOP1 0x11111111
#define LOOP2 0x22222222
#define LOOP3 0x33333333
#define LOOP4 0x44444444

#define SST_DEFAULT_PMIC_PORT 1 /*audio port*/
/* NOTE: status will have +ve for good cases and -ve for error ones */
#define MAX_STREAM_FIELD 255

int sst_alloc_stream(char *params, unsigned int stream_ops, u8 codec,
						unsigned int session_id);
int sst_alloc_stream_response(unsigned int str_id,
				struct snd_sst_alloc_response *response);
int sst_stalled(void);
int sst_pause_stream(int id);
int sst_resume_stream(int id);
int sst_enable_rx_timeslot(int status);
int sst_drop_stream(int id);
int sst_free_stream(int id);
int sst_start_stream(int streamID);
int sst_play_frame(int streamID);
int sst_pcm_play_frame(int str_id, struct sst_stream_bufs *sst_buf);
int sst_capture_frame(int streamID);
int sst_set_stream_param(int streamID, struct snd_sst_params *str_param);
int sst_target_device_select(struct snd_sst_target_device *target_device);
int sst_decode(int str_id, struct snd_sst_dbufs *dbufs);
int sst_get_decoded_bytes(int str_id, unsigned long long *bytes);
int sst_get_fw_info(struct snd_sst_fw_info *info);
int sst_get_stream_params(int str_id,
		struct snd_sst_get_stream_params *get_params);
int sst_get_stream(struct snd_sst_params *str_param);
int sst_get_stream_allocated(struct snd_sst_params *str_param,
				struct snd_sst_lib_download **lib_dnld);
int sst_drain_stream(int str_id);
int sst_get_vol(struct snd_sst_vol *set_vol);
int sst_set_vol(struct snd_sst_vol *set_vol);
int sst_set_mute(struct snd_sst_mute *set_mute);


void sst_post_message(struct work_struct *work);
void sst_process_message(struct work_struct *work);
void sst_process_reply(struct work_struct *work);
void sst_process_mad_ops(struct work_struct *work);
void sst_process_mad_jack_detection(struct work_struct *work);

long intel_sst_ioctl(struct file *file_ptr, unsigned int cmd,
			unsigned long arg);
int intel_sst_open(struct inode *i_node, struct file *file_ptr);
int intel_sst_open_cntrl(struct inode *i_node, struct file *file_ptr);
int intel_sst_release(struct inode *i_node, struct file *file_ptr);
int intel_sst_release_cntrl(struct inode *i_node, struct file *file_ptr);
int intel_sst_read(struct file *file_ptr, char __user *buf,
			size_t count, loff_t *ppos);
int intel_sst_write(struct file *file_ptr, const char __user *buf,
			size_t count, loff_t *ppos);
int intel_sst_mmap(struct file *fp, struct vm_area_struct *vma);
ssize_t intel_sst_aio_write(struct kiocb *kiocb, const struct iovec *iov,
			unsigned long nr_segs, loff_t  offset);
ssize_t intel_sst_aio_read(struct kiocb *kiocb, const struct iovec *iov,
			unsigned long nr_segs, loff_t offset);

int sst_load_fw(const struct firmware *fw, void *context);
int sst_load_library(struct snd_sst_lib_download *lib, u8 ops);
int sst_spi_mode_enable(void);
int sst_get_block_stream(struct intel_sst_drv *sst_drv_ctx);

int sst_wait_interruptible(struct intel_sst_drv *sst_drv_ctx,
				struct sst_block *block);
int sst_wait_interruptible_timeout(struct intel_sst_drv *sst_drv_ctx,
		struct sst_block *block, int timeout);
int sst_wait_timeout(struct intel_sst_drv *sst_drv_ctx,
		struct stream_alloc_block *block);
int sst_create_large_msg(struct ipc_post **arg);
int sst_create_short_msg(struct ipc_post **arg);
void sst_wake_up_alloc_block(struct intel_sst_drv *sst_drv_ctx,
		u8 sst_id, int status, void *data);
void sst_clear_interrupt(void);
int intel_sst_resume(struct pci_dev *pci);
int sst_download_fw(void);
void free_stream_context(unsigned int str_id);
void sst_clean_stream(struct stream_info *stream);

/*
 * sst_fill_header - inline to fill sst header
 *
 * @header : ipc header
 * @msg : IPC message to be sent
 * @large : is ipc large msg
 * @str_id : stream id
 *
 * this function is an inline function that sets the headers before
 * sending a message
 */
static inline void sst_fill_header(union ipc_header *header,
				int msg, int large, int str_id)
{
	header->part.msg_id = msg;
	header->part.str_id = str_id;
	header->part.large = large;
	header->part.done = 0;
	header->part.busy = 1;
	header->part.data = 0;
}

/*
 * sst_assign_pvt_id - assign a pvt id for stream
 *
 * @sst_drv_ctx : driver context
 *
 * this inline function assigns a private id for calls that dont have stream
 * context yet, should be called with lock held
 */
static inline unsigned int sst_assign_pvt_id(struct intel_sst_drv *sst_drv_ctx)
{
	sst_drv_ctx->unique_id++;
	if (sst_drv_ctx->unique_id >= MAX_NUM_STREAMS)
		sst_drv_ctx->unique_id = 1;
	return sst_drv_ctx->unique_id;
}

/*
 * sst_init_stream - this function initialzes stream context
 *
 * @stream : stream struture
 * @codec : codec for stream
 * @sst_id : stream id
 * @ops : stream operation
 * @slot : stream pcm slot
 * @device : device type
 *
 * this inline function initialzes stream context for allocated stream
 */
static inline void sst_init_stream(struct stream_info *stream,
		int codec, int sst_id, int ops, u8 slot,
		enum snd_sst_audio_device_type device)
{
	stream->status = STREAM_INIT;
	stream->prev = STREAM_UN_INIT;
	stream->codec = codec;
	stream->sst_id = sst_id;
	stream->str_type = 0;
	stream->ops = ops;
	stream->data_blk.on = false;
	stream->data_blk.condition = false;
	stream->data_blk.ret_code = 0;
	stream->data_blk.data = NULL;
	stream->ctrl_blk.on = false;
	stream->ctrl_blk.condition = false;
	stream->ctrl_blk.ret_code = 0;
	stream->ctrl_blk.data = NULL;
	stream->need_draining = false;
	stream->decode_ibuf = NULL;
	stream->decode_isize = 0;
	stream->mmapped = false;
	stream->pcm_slot = slot;
	stream->device = device;
}


/*
 * sst_validate_strid - this function validates the stream id
 *
 * @str_id : stream id to be validated
 *
 * returns 0 if valid stream
 */
static inline int sst_validate_strid(int str_id)
{
	if (str_id <= 0 || str_id > sst_drv_ctx->max_streams) {
		pr_err("SST ERR: invalid stream id : %d MAX_STREAMS:%d\n",
					str_id, sst_drv_ctx->max_streams);
		return -EINVAL;
	} else
		return 0;
}

static inline int sst_shim_write(void __iomem *addr, int offset, int value)
{

	if (sst_drv_ctx->pci_id == SST_MRST_PCI_ID)
		writel(value, addr + SST_ISRD);	/*dummy*/
	writel(value, addr + offset);
	return 0;
}

static inline int sst_shim_read(void __iomem *addr, int offset)
{
	return readl(addr + offset);
}
#endif /* __INTEL_SST_COMMON_H__ */
