/*
 * ngene.h: nGene PCIe bridge driver
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _NGENE_H_
#define _NGENE_H_

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <asm/dma.h>
#include <linux/scatterlist.h>

#include <linux/dvb/frontend.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_ca_en50221.h"
#include "dvb_frontend.h"
#include "dvb_ringbuffer.h"
#include "dvb_net.h"
#include "cxd2099.h"

#define DEVICE_NAME "ngene"

#define NGENE_VID       0x18c3
#define NGENE_PID       0x0720

#ifndef VIDEO_CAP_VC1
#define VIDEO_CAP_AVC   128
#define VIDEO_CAP_H264  128
#define VIDEO_CAP_VC1   256
#define VIDEO_CAP_WMV9  256
#define VIDEO_CAP_MPEG4 512
#endif

enum STREAM {
	STREAM_VIDEOIN1 = 0,        /* ITU656 or TS Input */
	STREAM_VIDEOIN2,
	STREAM_AUDIOIN1,            /* I2S or SPI Input */
	STREAM_AUDIOIN2,
	STREAM_AUDIOOUT,
	MAX_STREAM
};

enum SMODE_BITS {
	SMODE_AUDIO_SPDIF = 0x20,
	SMODE_AVSYNC = 0x10,
	SMODE_TRANSPORT_STREAM = 0x08,
	SMODE_AUDIO_CAPTURE = 0x04,
	SMODE_VBI_CAPTURE = 0x02,
	SMODE_VIDEO_CAPTURE = 0x01
};

enum STREAM_FLAG_BITS {
	SFLAG_CHROMA_FORMAT_2COMP  = 0x01, /* Chroma Format : 2's complement */
	SFLAG_CHROMA_FORMAT_OFFSET = 0x00, /* Chroma Format : Binary offset */
	SFLAG_ORDER_LUMA_CHROMA    = 0x02, /* Byte order: Y,Cb,Y,Cr */
	SFLAG_ORDER_CHROMA_LUMA    = 0x00, /* Byte order: Cb,Y,Cr,Y */
	SFLAG_COLORBAR             = 0x04, /* Select colorbar */
};

#define PROGRAM_ROM     0x0000
#define PROGRAM_SRAM    0x1000
#define PERIPHERALS0    0x8000
#define PERIPHERALS1    0x9000
#define SHARED_BUFFER   0xC000

#define HOST_TO_NGENE    (SHARED_BUFFER+0x0000)
#define NGENE_TO_HOST    (SHARED_BUFFER+0x0100)
#define NGENE_COMMAND    (SHARED_BUFFER+0x0200)
#define NGENE_COMMAND_HI (SHARED_BUFFER+0x0204)
#define NGENE_STATUS     (SHARED_BUFFER+0x0208)
#define NGENE_STATUS_HI  (SHARED_BUFFER+0x020C)
#define NGENE_EVENT      (SHARED_BUFFER+0x0210)
#define NGENE_EVENT_HI   (SHARED_BUFFER+0x0214)
#define VARIABLES        (SHARED_BUFFER+0x0210)

#define NGENE_INT_COUNTS       (SHARED_BUFFER+0x0260)
#define NGENE_INT_ENABLE       (SHARED_BUFFER+0x0264)
#define NGENE_VBI_LINE_COUNT   (SHARED_BUFFER+0x0268)

#define BUFFER_GP_XMIT  (SHARED_BUFFER+0x0800)
#define BUFFER_GP_RECV  (SHARED_BUFFER+0x0900)
#define EEPROM_AREA     (SHARED_BUFFER+0x0A00)

#define SG_V_IN_1       (SHARED_BUFFER+0x0A80)
#define SG_VBI_1        (SHARED_BUFFER+0x0B00)
#define SG_A_IN_1       (SHARED_BUFFER+0x0B80)
#define SG_V_IN_2       (SHARED_BUFFER+0x0C00)
#define SG_VBI_2        (SHARED_BUFFER+0x0C80)
#define SG_A_IN_2       (SHARED_BUFFER+0x0D00)
#define SG_V_OUT        (SHARED_BUFFER+0x0D80)
#define SG_A_OUT2       (SHARED_BUFFER+0x0E00)

#define DATA_A_IN_1     (SHARED_BUFFER+0x0E80)
#define DATA_A_IN_2     (SHARED_BUFFER+0x0F00)
#define DATA_A_OUT      (SHARED_BUFFER+0x0F80)
#define DATA_V_IN_1     (SHARED_BUFFER+0x1000)
#define DATA_V_IN_2     (SHARED_BUFFER+0x2000)
#define DATA_V_OUT      (SHARED_BUFFER+0x3000)

#define DATA_FIFO_AREA  (SHARED_BUFFER+0x1000)

#define TIMESTAMPS      0xA000
#define SCRATCHPAD      0xA080
#define FORCE_INT       0xA088
#define FORCE_NMI       0xA090
#define INT_STATUS      0xA0A0

#define DEV_VER         0x9004

#define FW_DEBUG_DEFAULT (PROGRAM_SRAM+0x00FF)

struct SG_ADDR {
	u64 start;
	u64 curr;
	u16 curr_ptr;
	u16 elements;
	u32 pad[3];
} __attribute__ ((__packed__));

struct SHARED_MEMORY {
	/* C000 */
	u32 HostToNgene[64];

	/* C100 */
	u32 NgeneToHost[64];

	/* C200 */
	u64 NgeneCommand;
	u64 NgeneStatus;
	u64 NgeneEvent;

	/* C210 */
	u8 pad1[0xc260 - 0xc218];

	/* C260 */
	u32 IntCounts;
	u32 IntEnable;

	/* C268 */
	u8 pad2[0xd000 - 0xc268];

} __attribute__ ((__packed__));

struct BUFFER_STREAM_RESULTS {
	u32 Clock;           /* Stream time in 100ns units */
	u16 RemainingLines;  /* Remaining lines in this field.
				0 for complete field */
	u8  FieldCount;      /* Video field number */
	u8  Flags;           /* Bit 7 = Done, Bit 6 = seen, Bit 5 = overflow,
				Bit 0 = FieldID */
	u16 BlockCount;      /* Audio block count (unused) */
	u8  Reserved[2];
	u32 DTOUpdate;
} __attribute__ ((__packed__));

struct HW_SCATTER_GATHER_ELEMENT {
	u64 Address;
	u32 Length;
	u32 Reserved;
} __attribute__ ((__packed__));

struct BUFFER_HEADER {
	u64    Next;
	struct BUFFER_STREAM_RESULTS SR;

	u32    Number_of_entries_1;
	u32    Reserved5;
	u64    Address_of_first_entry_1;

	u32    Number_of_entries_2;
	u32    Reserved7;
	u64    Address_of_first_entry_2;
} __attribute__ ((__packed__));

struct EVENT_BUFFER {
	u32    TimeStamp;
	u8     GPIOStatus;
	u8     UARTStatus;
	u8     RXCharacter;
	u8     EventStatus;
	u32    Reserved[2];
} __attribute__ ((__packed__));

/* Firmware commands. */

enum OPCODES {
	CMD_NOP = 0,
	CMD_FWLOAD_PREPARE  = 0x01,
	CMD_FWLOAD_FINISH   = 0x02,
	CMD_I2C_READ        = 0x03,
	CMD_I2C_WRITE       = 0x04,

	CMD_I2C_WRITE_NOSTOP = 0x05,
	CMD_I2C_CONTINUE_WRITE = 0x06,
	CMD_I2C_CONTINUE_WRITE_NOSTOP = 0x07,

	CMD_DEBUG_OUTPUT    = 0x09,

	CMD_CONTROL         = 0x10,
	CMD_CONFIGURE_BUFFER = 0x11,
	CMD_CONFIGURE_FREE_BUFFER = 0x12,

	CMD_SPI_READ        = 0x13,
	CMD_SPI_WRITE       = 0x14,

	CMD_MEM_READ        = 0x20,
	CMD_MEM_WRITE	    = 0x21,
	CMD_SFR_READ	    = 0x22,
	CMD_SFR_WRITE	    = 0x23,
	CMD_IRAM_READ	    = 0x24,
	CMD_IRAM_WRITE	    = 0x25,
	CMD_SET_GPIO_PIN    = 0x26,
	CMD_SET_GPIO_INT    = 0x27,
	CMD_CONFIGURE_UART  = 0x28,
	CMD_WRITE_UART      = 0x29,
	MAX_CMD
};

enum RESPONSES {
	OK = 0,
	ERROR = 1
};

struct FW_HEADER {
	u8 Opcode;
	u8 Length;
} __attribute__ ((__packed__));

struct FW_I2C_WRITE {
	struct FW_HEADER hdr;
	u8 Device;
	u8 Data[250];
} __attribute__ ((__packed__));

struct FW_I2C_CONTINUE_WRITE {
	struct FW_HEADER hdr;
	u8 Data[250];
} __attribute__ ((__packed__));

struct FW_I2C_READ {
	struct FW_HEADER hdr;
	u8 Device;
	u8 Data[252];    /* followed by two bytes of read data count */
} __attribute__ ((__packed__));

struct FW_SPI_WRITE {
	struct FW_HEADER hdr;
	u8 ModeSelect;
	u8 Data[250];
} __attribute__ ((__packed__));

struct FW_SPI_READ {
	struct FW_HEADER hdr;
	u8 ModeSelect;
	u8 Data[252];    /* followed by two bytes of read data count */
} __attribute__ ((__packed__));

struct FW_FWLOAD_PREPARE {
	struct FW_HEADER hdr;
} __attribute__ ((__packed__));

struct FW_FWLOAD_FINISH {
	struct FW_HEADER hdr;
	u16 Address;     /* address of final block */
	u16 Length;
} __attribute__ ((__packed__));

/*
 * Meaning of FW_STREAM_CONTROL::Mode bits:
 *  Bit 7: Loopback PEXin to PEXout using TVOut channel
 *  Bit 6: AVLOOP
 *  Bit 5: Audio select; 0=I2S, 1=SPDIF
 *  Bit 4: AVSYNC
 *  Bit 3: Enable transport stream
 *  Bit 2: Enable audio capture
 *  Bit 1: Enable ITU-Video VBI capture
 *  Bit 0: Enable ITU-Video capture
 *
 * Meaning of FW_STREAM_CONTROL::Control bits (see UVI1_CTL)
 *  Bit 7: continuous capture
 *  Bit 6: capture one field
 *  Bit 5: capture one frame
 *  Bit 4: unused
 *  Bit 3: starting field; 0=odd, 1=even
 *  Bit 2: sample size; 0=8-bit, 1=10-bit
 *  Bit 1: data format; 0=UYVY, 1=YUY2
 *  Bit 0: resets buffer pointers
*/

enum FSC_MODE_BITS {
	SMODE_LOOPBACK          = 0x80,
	SMODE_AVLOOP            = 0x40,
	_SMODE_AUDIO_SPDIF      = 0x20,
	_SMODE_AVSYNC           = 0x10,
	_SMODE_TRANSPORT_STREAM = 0x08,
	_SMODE_AUDIO_CAPTURE    = 0x04,
	_SMODE_VBI_CAPTURE      = 0x02,
	_SMODE_VIDEO_CAPTURE    = 0x01
};


/* Meaning of FW_STREAM_CONTROL::Stream bits:
 * Bit 3: Audio sample count:  0 = relative, 1 = absolute
 * Bit 2: color bar select; 1=color bars, 0=CV3 decoder
 * Bits 1-0: stream select, UVI1, UVI2, TVOUT
 */

struct FW_STREAM_CONTROL {
	struct FW_HEADER hdr;
	u8     Stream;             /* Stream number (UVI1, UVI2, TVOUT) */
	u8     Control;            /* Value written to UVI1_CTL */
	u8     Mode;               /* Controls clock source */
	u8     SetupDataLen;	   /* Length of setup data, MSB=1 write
				      backwards */
	u16    CaptureBlockCount;  /* Blocks (a 256 Bytes) to capture per buffer
				      for TS and Audio */
	u64    Buffer_Address;	   /* Address of first buffer header */
	u16    BytesPerVideoLine;
	u16    MaxLinesPerField;
	u16    MinLinesPerField;
	u16    Reserved_1;
	u16    BytesPerVBILine;
	u16    MaxVBILinesPerField;
	u16    MinVBILinesPerField;
	u16    SetupDataAddr;      /* ngene relative address of setup data */
	u8     SetupData[32];      /* setup data */
} __attribute__((__packed__));

#define AUDIO_BLOCK_SIZE    256
#define TS_BLOCK_SIZE       256

struct FW_MEM_READ {
	struct FW_HEADER hdr;
	u16   address;
} __attribute__ ((__packed__));

struct FW_MEM_WRITE {
	struct FW_HEADER hdr;
	u16   address;
	u8    data;
} __attribute__ ((__packed__));

struct FW_SFR_IRAM_READ {
	struct FW_HEADER hdr;
	u8    address;
} __attribute__ ((__packed__));

struct FW_SFR_IRAM_WRITE {
	struct FW_HEADER hdr;
	u8    address;
	u8    data;
} __attribute__ ((__packed__));

struct FW_SET_GPIO_PIN {
	struct FW_HEADER hdr;
	u8    select;
} __attribute__ ((__packed__));

struct FW_SET_GPIO_INT {
	struct FW_HEADER hdr;
	u8    select;
} __attribute__ ((__packed__));

struct FW_SET_DEBUGMODE {
	struct FW_HEADER hdr;
	u8   debug_flags;
} __attribute__ ((__packed__));

struct FW_CONFIGURE_BUFFERS {
	struct FW_HEADER hdr;
	u8   config;
} __attribute__ ((__packed__));

enum _BUFFER_CONFIGS {
	/* 4k UVI1, 4k UVI2, 2k AUD1, 2k AUD2  (standard usage) */
	BUFFER_CONFIG_4422 = 0,
	/* 3k UVI1, 3k UVI2, 3k AUD1, 3k AUD2  (4x TS input usage) */
	BUFFER_CONFIG_3333 = 1,
	/* 8k UVI1, 0k UVI2, 2k AUD1, 2k I2SOut  (HDTV decoder usage) */
	BUFFER_CONFIG_8022 = 2,
	BUFFER_CONFIG_FW17 = 255, /* Use new FW 17 command */
};

struct FW_CONFIGURE_FREE_BUFFERS {
	struct FW_HEADER hdr;
	u8   UVI1_BufferLength;
	u8   UVI2_BufferLength;
	u8   TVO_BufferLength;
	u8   AUD1_BufferLength;
	u8   AUD2_BufferLength;
	u8   TVA_BufferLength;
} __attribute__ ((__packed__));

struct FW_CONFIGURE_UART {
	struct FW_HEADER hdr;
	u8 UartControl;
} __attribute__ ((__packed__));

enum _UART_CONFIG {
	_UART_BAUDRATE_19200 = 0,
	_UART_BAUDRATE_9600  = 1,
	_UART_BAUDRATE_4800  = 2,
	_UART_BAUDRATE_2400  = 3,
	_UART_RX_ENABLE      = 0x40,
	_UART_TX_ENABLE      = 0x80,
};

struct FW_WRITE_UART {
	struct FW_HEADER hdr;
	u8 Data[252];
} __attribute__ ((__packed__));


struct ngene_command {
	u32 in_len;
	u32 out_len;
	union {
		u32                              raw[64];
		u8                               raw8[256];
		struct FW_HEADER                 hdr;
		struct FW_I2C_WRITE              I2CWrite;
		struct FW_I2C_CONTINUE_WRITE     I2CContinueWrite;
		struct FW_I2C_READ               I2CRead;
		struct FW_STREAM_CONTROL         StreamControl;
		struct FW_FWLOAD_PREPARE         FWLoadPrepare;
		struct FW_FWLOAD_FINISH          FWLoadFinish;
		struct FW_MEM_READ		 MemoryRead;
		struct FW_MEM_WRITE		 MemoryWrite;
		struct FW_SFR_IRAM_READ		 SfrIramRead;
		struct FW_SFR_IRAM_WRITE         SfrIramWrite;
		struct FW_SPI_WRITE              SPIWrite;
		struct FW_SPI_READ               SPIRead;
		struct FW_SET_GPIO_PIN           SetGpioPin;
		struct FW_SET_GPIO_INT           SetGpioInt;
		struct FW_SET_DEBUGMODE          SetDebugMode;
		struct FW_CONFIGURE_BUFFERS      ConfigureBuffers;
		struct FW_CONFIGURE_FREE_BUFFERS ConfigureFreeBuffers;
		struct FW_CONFIGURE_UART         ConfigureUart;
		struct FW_WRITE_UART             WriteUart;
	} cmd;
} __attribute__ ((__packed__));

#define NGENE_INTERFACE_VERSION 0x103
#define MAX_VIDEO_BUFFER_SIZE   (417792) /* 288*1440 rounded up to next page */
#define MAX_AUDIO_BUFFER_SIZE     (8192) /* Gives room for about 23msec@48KHz */
#define MAX_VBI_BUFFER_SIZE      (28672) /* 1144*18 rounded up to next page */
#define MAX_TS_BUFFER_SIZE       (98304) /* 512*188 rounded up to next page */
#define MAX_HDTV_BUFFER_SIZE   (2080768) /* 541*1920*2 rounded up to next page
					    Max: (1920x1080i60) */

#define OVERFLOW_BUFFER_SIZE    (8192)

#define RING_SIZE_VIDEO     4
#define RING_SIZE_AUDIO     8
#define RING_SIZE_TS        8

#define NUM_SCATTER_GATHER_ENTRIES  8

#define MAX_DMA_LENGTH (((MAX_VIDEO_BUFFER_SIZE + MAX_VBI_BUFFER_SIZE) * \
			RING_SIZE_VIDEO * 2) + \
			(MAX_AUDIO_BUFFER_SIZE * RING_SIZE_AUDIO * 2) + \
			(MAX_TS_BUFFER_SIZE * RING_SIZE_TS * 4) + \
			(RING_SIZE_VIDEO * PAGE_SIZE * 2) + \
			(RING_SIZE_AUDIO * PAGE_SIZE * 2) + \
			(RING_SIZE_TS    * PAGE_SIZE * 4) + \
			 8 * PAGE_SIZE + OVERFLOW_BUFFER_SIZE + PAGE_SIZE)

#define EVENT_QUEUE_SIZE    16

/* Gathers the current state of a single channel. */

struct SBufferHeader {
	struct BUFFER_HEADER   ngeneBuffer; /* Physical descriptor */
	struct SBufferHeader  *Next;
	void                  *Buffer1;
	struct HW_SCATTER_GATHER_ELEMENT *scList1;
	void                  *Buffer2;
	struct HW_SCATTER_GATHER_ELEMENT *scList2;
};

/* Sizeof SBufferHeader aligned to next 64 Bit boundary (hw restriction) */
#define SIZEOF_SBufferHeader ((sizeof(struct SBufferHeader) + 63) & ~63)

enum HWSTATE {
	HWSTATE_STOP,
	HWSTATE_STARTUP,
	HWSTATE_RUN,
	HWSTATE_PAUSE,
};

enum KSSTATE {
	KSSTATE_STOP,
	KSSTATE_ACQUIRE,
	KSSTATE_PAUSE,
	KSSTATE_RUN,
};

struct SRingBufferDescriptor {
	struct SBufferHeader *Head; /* Points to first buffer in ring buffer
				       structure*/
	u64   PAHead;         /* Physical address of first buffer */
	u32   MemSize;        /* Memory size of allocated ring buffers
				 (needed for freeing) */
	u32   NumBuffers;     /* Number of buffers in the ring */
	u32   Buffer1Length;  /* Allocated length of Buffer 1 */
	u32   Buffer2Length;  /* Allocated length of Buffer 2 */
	void *SCListMem;      /* Memory to hold scatter gather lists for this
				 ring */
	u64   PASCListMem;    /* Physical address  .. */
	u32   SCListMemSize;  /* Size of this memory */
};

enum STREAMMODEFLAGS {
	StreamMode_NONE   = 0, /* Stream not used */
	StreamMode_ANALOG = 1, /* Analog: Stream 0,1 = Video, 2,3 = Audio */
	StreamMode_TSIN   = 2, /* Transport stream input (all) */
	StreamMode_HDTV   = 4, /* HDTV: Maximum 1920x1080p30,1920x1080i60
				  (only stream 0) */
	StreamMode_TSOUT  = 8, /* Transport stream output (only stream 3) */
};


enum BufferExchangeFlags {
	BEF_EVEN_FIELD   = 0x00000001,
	BEF_CONTINUATION = 0x00000002,
	BEF_MORE_DATA    = 0x00000004,
	BEF_OVERFLOW     = 0x00000008,
	DF_SWAP32        = 0x00010000,
};

typedef void *(IBufferExchange)(void *, void *, u32, u32, u32);

struct MICI_STREAMINFO {
	IBufferExchange    *pExchange;
	IBufferExchange    *pExchangeVBI;     /* Secondary (VBI, ancillary) */
	u8  Stream;
	u8  Flags;
	u8  Mode;
	u8  Reserved;
	u16 nLinesVideo;
	u16 nBytesPerLineVideo;
	u16 nLinesVBI;
	u16 nBytesPerLineVBI;
	u32 CaptureLength;    /* Used for audio and transport stream */
};

/****************************************************************************/
/* STRUCTS ******************************************************************/
/****************************************************************************/

/* sound hardware definition */
#define MIXER_ADDR_TVTUNER      0
#define MIXER_ADDR_LAST         0

struct ngene_channel;

/*struct sound chip*/

struct mychip {
	struct ngene_channel *chan;
	struct snd_card *card;
	struct pci_dev *pci;
	struct snd_pcm_substream *substream;
	struct snd_pcm *pcm;
	unsigned long port;
	int irq;
	spinlock_t mixer_lock;
	spinlock_t lock;
	int mixer_volume[MIXER_ADDR_LAST + 1][2];
	int capture_source[MIXER_ADDR_LAST + 1][2];
};

#ifdef NGENE_V4L
struct ngene_overlay {
	int                    tvnorm;
	struct v4l2_rect       w;
	enum v4l2_field        field;
	struct v4l2_clip       *clips;
	int                    nclips;
	int                    setup_ok;
};

struct ngene_tvnorm {
	int   v4l2_id;
	char  *name;
	u16   swidth, sheight; /* scaled standard width, height */
	int   tuner_norm;
	int   soundstd;
};

struct ngene_vopen {
	struct ngene_channel      *ch;
	enum v4l2_priority         prio;
	int                        width;
	int                        height;
	int                        depth;
	struct videobuf_queue      vbuf_q;
	struct videobuf_queue      vbi;
	int                        fourcc;
	int                        picxcount;
	int                        resources;
	enum v4l2_buf_type         type;
	const struct ngene_format *fmt;

	const struct ngene_format *ovfmt;
	struct ngene_overlay       ov;
};
#endif

struct ngene_channel {
	struct device         device;
	struct i2c_adapter    i2c_adapter;

	struct ngene         *dev;
	int                   number;
	int                   type;
	int                   mode;
	bool                  has_adapter;
	bool                  has_demux;
	int                   demod_type;
	int (*gate_ctrl)(struct dvb_frontend *, int);

	struct dvb_frontend  *fe;
	struct dvb_frontend  *fe2;
	struct dmxdev         dmxdev;
	struct dvb_demux      demux;
	struct dvb_net        dvbnet;
	struct dmx_frontend   hw_frontend;
	struct dmx_frontend   mem_frontend;
	int                   users;
	struct video_device  *v4l_dev;
	struct dvb_device    *ci_dev;
	struct tasklet_struct demux_tasklet;

	struct SBufferHeader *nextBuffer;
	enum KSSTATE          State;
	enum HWSTATE          HWState;
	u8                    Stream;
	u8                    Flags;
	u8                    Mode;
	IBufferExchange      *pBufferExchange;
	IBufferExchange      *pBufferExchange2;

	spinlock_t            state_lock;
	u16                   nLines;
	u16                   nBytesPerLine;
	u16                   nVBILines;
	u16                   nBytesPerVBILine;
	u16                   itumode;
	u32                   Capture1Length;
	u32                   Capture2Length;
	struct SRingBufferDescriptor RingBuffer;
	struct SRingBufferDescriptor TSRingBuffer;
	struct SRingBufferDescriptor TSIdleBuffer;

	u32                   DataFormatFlags;

	int                   AudioDTOUpdated;
	u32                   AudioDTOValue;

	int (*set_tone)(struct dvb_frontend *, fe_sec_tone_mode_t);
	u8 lnbh;

	/* stuff from analog driver */

	int minor;
	struct mychip        *mychip;
	struct snd_card      *soundcard;
	u8                   *evenbuffer;
	u8                    dma_on;
	int                   soundstreamon;
	int                   audiomute;
	int                   soundbuffisallocated;
	int                   sndbuffflag;
	int                   tun_rdy;
	int                   dec_rdy;
	int                   tun_dec_rdy;
	int                   lastbufferflag;

	struct ngene_tvnorm  *tvnorms;
	int                   tvnorm_num;
	int                   tvnorm;

#ifdef NGENE_V4L
	int                   videousers;
	struct v4l2_prio_state prio;
	struct ngene_vopen    init;
	int                   resources;
	struct v4l2_framebuffer fbuf;
	struct ngene_buffer  *screen;     /* overlay             */
	struct list_head      capture;    /* video capture queue */
	spinlock_t s_lock;
	struct semaphore reslock;
#endif

	int running;
};


struct ngene_ci {
	struct device         device;
	struct i2c_adapter    i2c_adapter;

	struct ngene         *dev;
	struct dvb_ca_en50221 *en;
};

struct ngene;

typedef void (rx_cb_t)(struct ngene *, u32, u8);
typedef void (tx_cb_t)(struct ngene *, u32);

struct ngene {
	int                   nr;
	struct pci_dev       *pci_dev;
	unsigned char        *iomem;

	/*struct i2c_adapter  i2c_adapter;*/

	u32                   device_version;
	u32                   fw_interface_version;
	u32                   icounts;
	bool                  msi_enabled;
	bool                  cmd_timeout_workaround;

	u8                   *CmdDoneByte;
	int                   BootFirmware;
	void                 *OverflowBuffer;
	dma_addr_t            PAOverflowBuffer;
	void                 *FWInterfaceBuffer;
	dma_addr_t            PAFWInterfaceBuffer;
	u8                   *ngenetohost;
	u8                   *hosttongene;

	struct EVENT_BUFFER   EventQueue[EVENT_QUEUE_SIZE];
	int                   EventQueueOverflowCount;
	int                   EventQueueOverflowFlag;
	struct tasklet_struct event_tasklet;
	struct EVENT_BUFFER  *EventBuffer;
	int                   EventQueueWriteIndex;
	int                   EventQueueReadIndex;

	wait_queue_head_t     cmd_wq;
	int                   cmd_done;
	struct semaphore      cmd_mutex;
	struct semaphore      stream_mutex;
	struct semaphore      pll_mutex;
	struct semaphore      i2c_switch_mutex;
	int                   i2c_current_channel;
	int                   i2c_current_bus;
	spinlock_t            cmd_lock;

	struct dvb_adapter    adapter[MAX_STREAM];
	struct dvb_adapter    *first_adapter; /* "one_adapter" modprobe opt */
	struct ngene_channel  channel[MAX_STREAM];

	struct ngene_info    *card_info;

	tx_cb_t              *TxEventNotify;
	rx_cb_t              *RxEventNotify;
	int                   tx_busy;
	wait_queue_head_t     tx_wq;
	wait_queue_head_t     rx_wq;
#define UART_RBUF_LEN 4096
	u8                    uart_rbuf[UART_RBUF_LEN];
	int                   uart_rp, uart_wp;

#define TS_FILLER  0x6f

	u8                   *tsout_buf;
#define TSOUT_BUF_SIZE (512*188*8)
	struct dvb_ringbuffer tsout_rbuf;

	u8                   *tsin_buf;
#define TSIN_BUF_SIZE (512*188*8)
	struct dvb_ringbuffer tsin_rbuf;

	u8                   *ain_buf;
#define AIN_BUF_SIZE (128*1024)
	struct dvb_ringbuffer ain_rbuf;


	u8                   *vin_buf;
#define VIN_BUF_SIZE (4*1920*1080)
	struct dvb_ringbuffer vin_rbuf;

	unsigned long         exp_val;
	int prev_cmd;

	struct ngene_ci       ci;
};

struct ngene_info {
	int   type;
#define NGENE_APP        0
#define NGENE_TERRATEC   1
#define NGENE_SIDEWINDER 2
#define NGENE_RACER      3
#define NGENE_VIPER      4
#define NGENE_PYTHON     5
#define NGENE_VBOX_V1	 6
#define NGENE_VBOX_V2	 7

	int   fw_version;
	bool  msi_supported;
	char *name;

	int   io_type[MAX_STREAM];
#define NGENE_IO_NONE    0
#define NGENE_IO_TV      1
#define NGENE_IO_HDTV    2
#define NGENE_IO_TSIN    4
#define NGENE_IO_TSOUT   8
#define NGENE_IO_AIN     16

	void *fe_config[4];
	void *tuner_config[4];

	int (*demod_attach[4])(struct ngene_channel *);
	int (*tuner_attach[4])(struct ngene_channel *);

	u8    avf[4];
	u8    msp[4];
	u8    demoda[4];
	u8    lnb[4];
	int   i2c_access;
	u8    ntsc;
	u8    tsf[4];
	u8    i2s[4];

	int (*gate_ctrl)(struct dvb_frontend *, int);
	int (*switch_ctrl)(struct ngene_channel *, int, int);
};

#ifdef NGENE_V4L
struct ngene_format {
	char *name;
	int   fourcc;          /* video4linux 2      */
	int   btformat;        /* BT848_COLOR_FMT_*  */
	int   format;
	int   btswap;          /* BT848_COLOR_CTL_*  */
	int   depth;           /* bit/pixel          */
	int   flags;
	int   hshift, vshift;  /* for planar modes   */
	int   palette;
};

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

struct ngene_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer     vb;

	/* ngene specific */
	const struct ngene_format *fmt;
	int                        tvnorm;
	int                        btformat;
	int                        btswap;
};
#endif


/* Provided by ngene-core.c */
int __devinit ngene_probe(struct pci_dev *pci_dev,
			  const struct pci_device_id *id);
void __devexit ngene_remove(struct pci_dev *pdev);
void ngene_shutdown(struct pci_dev *pdev);
int ngene_command(struct ngene *dev, struct ngene_command *com);
int ngene_command_gpio_set(struct ngene *dev, u8 select, u8 level);
void set_transfer(struct ngene_channel *chan, int state);
void FillTSBuffer(void *Buffer, int Length, u32 Flags);

/* Provided by ngene-i2c.c */
int ngene_i2c_init(struct ngene *dev, int dev_nr);

/* Provided by ngene-dvb.c */
extern struct dvb_device ngene_dvbdev_ci;
void *tsout_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags);
void *tsin_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags);
int ngene_start_feed(struct dvb_demux_feed *dvbdmxfeed);
int ngene_stop_feed(struct dvb_demux_feed *dvbdmxfeed);
int my_dvb_dmx_ts_card_init(struct dvb_demux *dvbdemux, char *id,
			    int (*start_feed)(struct dvb_demux_feed *),
			    int (*stop_feed)(struct dvb_demux_feed *),
			    void *priv);
int my_dvb_dmxdev_ts_card_init(struct dmxdev *dmxdev,
			       struct dvb_demux *dvbdemux,
			       struct dmx_frontend *hw_frontend,
			       struct dmx_frontend *mem_frontend,
			       struct dvb_adapter *dvb_adapter);

#endif

/*  LocalWords:  Endif
 */
