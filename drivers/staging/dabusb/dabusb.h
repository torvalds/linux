#define _BULK_DATA_LEN 64
typedef struct
{
	unsigned char data[_BULK_DATA_LEN];
	unsigned int size;
	unsigned int pipe;
}bulk_transfer_t,*pbulk_transfer_t;

#define DABUSB_MINOR 240		/* some unassigned USB minor */
#define DABUSB_VERSION 0x1000
#define IOCTL_DAB_BULK              _IOWR('d', 0x30, bulk_transfer_t)
#define IOCTL_DAB_OVERRUNS	    _IOR('d',  0x15, int)
#define IOCTL_DAB_VERSION           _IOR('d', 0x3f, int)

#ifdef __KERNEL__

typedef enum { _stopped=0, _started } driver_state_t;

typedef struct
{
	struct mutex mutex;
	struct usb_device *usbdev;
	wait_queue_head_t wait;
	wait_queue_head_t remove_ok;
	spinlock_t lock;
	atomic_t pending_io;
	driver_state_t state;
	int remove_pending;
	int got_mem;
	int total_buffer_size;
	unsigned int overruns;
	int readptr;
	int opened;
	int devnum;
	struct list_head free_buff_list;
	struct list_head rec_buff_list;
} dabusb_t,*pdabusb_t;

typedef struct
{
	pdabusb_t s;
	struct urb *purb;
	struct list_head buff_list;
} buff_t,*pbuff_t;

typedef struct
{
	wait_queue_head_t wait;
} bulk_completion_context_t, *pbulk_completion_context_t;


#define _DABUSB_IF 2
#define _DABUSB_ISOPIPE 0x09
#define _ISOPIPESIZE	16384

#define _BULK_DATA_LEN 64
// Vendor specific request code for Anchor Upload/Download
// This one is implemented in the core
#define ANCHOR_LOAD_INTERNAL  0xA0

// EZ-USB Control and Status Register.  Bit 0 controls 8051 reset
#define CPUCS_REG    0x7F92
#define _TOTAL_BUFFERS 384

#define MAX_INTEL_HEX_RECORD_LENGTH 16

#ifndef _BYTE_DEFINED
#define _BYTE_DEFINED
typedef unsigned char BYTE;
#endif // !_BYTE_DEFINED

#ifndef _WORD_DEFINED
#define _WORD_DEFINED
typedef unsigned short WORD;
#endif // !_WORD_DEFINED

typedef struct _INTEL_HEX_RECORD
{
   BYTE  Length;
   WORD  Address;
   BYTE  Type;
   BYTE  Data[MAX_INTEL_HEX_RECORD_LENGTH];
} INTEL_HEX_RECORD, *PINTEL_HEX_RECORD;

#endif
