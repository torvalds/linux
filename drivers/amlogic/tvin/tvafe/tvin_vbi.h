/*******************************************************************
*  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
*  File name: tvin_vbi.h
*  Description: IO function, structure, enum, used in TVIN vbi sub-module processing
*******************************************************************/

#ifndef TVIN_VBI_H_
#define TVIN_VBI_H_

/* Standard Linux Headers */
#include <linux/cdev.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

// ***************************************************************************
// *** macro definitions *********************************************
// ***************************************************************************
/* defines for vbi spec */
//vbi type id
#define VBI_ID_USCC                0
#define VBI_ID_TT                  3
#define VBI_ID_WSS625              4
#define VBI_ID_WSSJ                5
#define VBI_ID_VPS                 6
#define MAX_PACKET_TYPE            VBI_ID_VPS

//vbi package data bytes
#define VBI_PCNT_USCC              2
#define VBI_PCNT_WSS625            2
#define VBI_PCNT_WSSJ              3
#define VBI_PCNT_VPS               8

//teletext package data bytes
#define VBI_PCNT_TT_625A           37
#define VBI_PCNT_TT_625B           42
#define VBI_PCNT_TT_625C           33
#define VBI_PCNT_TT_625D           34
#define VBI_PCNT_TT_525B           34
#define VBI_PCNT_TT_525C           33
#define VBI_PCNT_TT_525D           34

//Teletext system type
#define VBI_SYS_TT_625A            0
#define VBI_SYS_TT_625B            1
#define VBI_SYS_TT_625C            2
#define VBI_SYS_TT_625D            3
#define VBI_SYS_TT_525B            5
#define VBI_SYS_TT_525C            6
#define VBI_SYS_TT_525D            7


//vbi data type setting
#define VBI_DATA_TYPE_USCC         0x11
#define VBI_DATA_TYPE_EUROCC       0x22
#define VBI_DATA_TYPE_VPS          0x33
#define VBI_DATA_TYPE_TT_625A      0x55
#define VBI_DATA_TYPE_TT_625B      0x66
#define VBI_DATA_TYPE_TT_625C      0x77
#define VBI_DATA_TYPE_TT_625D      0x88
#define VBI_DATA_TYPE_TT_525B      0x99
#define VBI_DATA_TYPE_TT_525C      0xaa
#define VBI_DATA_TYPE_TT_525D      0xbb
#define VBI_DATA_TYPE_WSS625       0xcc
#define VBI_DATA_TYPE_WSSJ         0xdd

#define VBI_LINE_MIN               6
#define VBI_LINE_MAX               25

//closed caption type
#define VBI_PACKAGE_CC1            1
#define VBI_PACKAGE_CC2            2
#define VBI_PACKAGE_CC3            4
#define VBI_PACKAGE_CC4            8
#define VBI_PACKAGE_TT1            16
#define VBI_PACKAGE_TT2            32
#define VBI_PACKAGE_TT3            64
#define VBI_PACKAGE_TT4            128
#define VBI_PACKAGE_XDS            256

#define VBI_PACKAGE_FILTER_MAX     3

#define VBI_IOC_MAGIC 'X'

#define VBI_IOC_CC_EN              _IO (VBI_IOC_MAGIC, 0x01)
#define VBI_IOC_CC_DISABLE         _IO (VBI_IOC_MAGIC, 0x02)
#define VBI_IOC_SET_FILTER         _IOW(VBI_IOC_MAGIC, 0x03, int)
#define VBI_IOC_S_BUF_SIZE         _IOW(VBI_IOC_MAGIC, 0x04, int)
#define VBI_IOC_START              _IO (VBI_IOC_MAGIC, 0x05)
#define VBI_IOC_STOP               _IO (VBI_IOC_MAGIC, 0x06)


#define VBI_MEM_SIZE               0x1000//0x8000   // 32768 hw address with 8bit not 64bit
//#define VBI_SLICED_MAX            64   // 32768 hw address with 8bit not 64bit
#define VBI_WRITE_BURST_BYTE        8
#define SLICED_BUF_MAX              4096

//debug defines
#define VBI_CC_SUPPORT
//#define VBI_TT_SUPPORT


#define VBI_DATA_TYPE_LEN          16
#define VBI_DATA_TYPE_MASK         0xf0000

#define VBI_PAC_TYPE_LEN           0
#define VBI_PAC_TYPE_MASK          0x0ffff

#define VBI_PAC_CC_FIELD_LEN       4
#define VBI_PAC_CC_FIELD_MASK      0xf0
#define VBI_PAC_CC_FIELD1          0
#define VBI_PAC_CC_FIELD2          1

/* vbi type */
#define VBI_TYPE_NULL           0
#define VBI_TYPE_USCC           0x00001  //
#define VBI_TYPE_EUROCC         0x00020
#define VBI_TYPE_VPS            0x00040  //Germany, Austria and Switzerland.
#define VBI_TYPE_TT_625A        0x00080  //
#define VBI_TYPE_TT_625B        0x00100  //
#define VBI_TYPE_TT_625C        0x00200  //
#define VBI_TYPE_TT_625D        0x00400  //
#define VBI_TYPE_TT_525B        0x00800  //
#define VBI_TYPE_TT_525C        0x01000  //
#define VBI_TYPE_TT_525D        0x02000  //
#define VBI_TYPE_WSS625         0x04000  //
#define VBI_TYPE_WSSJ           0x08000  //

#define VBI_DEFAULT_BUFFER_SIZE 8192 //default buffer size
#define VBI_IRQ_EN //kuka add
#define VBI_TIMER_INTERVAL     (HZ/100)   //10ms, #define HZ 100 kuka add
#define VBI_BUF_DIV_EN //kuka add
#define VBI_ON_M6TV //VBI in M6TV IC only support CC format
#ifdef VBI_ON_M6TV
#undef VBI_IRQ_EN
#endif

// ***************************************************************************
// *** enum definitions *********************************************
// ***************************************************************************

typedef enum field_id_e {
    FIELD_1 = 0,
    FIELD_2 = 1,
} field_id_t;

enum vbi_state_e {
    VBI_STATE_FREE      = 0,
    VBI_STATE_ALLOCATED = 1,
    VBI_STATE_SET       = 2,
    VBI_STATE_GO        = 3,
    VBI_STATE_DONE      = 4,
    VBI_STATE_TIMEDOUT  = 5
} vbi_state_t;

// ***************************************************************************
// *** structure definitions *********************************************
// ***************************************************************************

typedef struct cc_data_s {
#ifdef VBI_ON_M6TV
	unsigned char b[2];         //       : 8;  // cc data1
#else
	unsigned int vbi_type :  8; // vbi data type: us_cc, teletext,wss_625,wssj,vps....
	unsigned int field_id :  8; // field type: 0:even; 1:odd;
	unsigned int nbytes   : 16; // data byte count: cc:two bytes; tt: depends on tt spec
	unsigned int line_num : 16; // vbi data line number
	unsigned char b[2];         //       : 8;  // cc data1
#endif
} cc_data_t;

struct vbi_ringbuffer_s {
    struct cc_data_s *data;
    ssize_t           size;
    ssize_t           pread;
    ssize_t           pwrite;
    int               error;

    wait_queue_head_t queue;
    spinlock_t        lock;
} vbi_ringbuffer_t;

typedef struct vbi_slicer_s {
    unsigned int           type;
    enum vbi_state_e        state;

    struct vbi_ringbuffer_s buffer;
    struct mutex            mutex;

    unsigned int           reserve;
} vbi_slicer_t;

/* vbi device structure */
typedef struct vbi_dev_s {
    int                   index;

    dev_t                 devt;
    struct cdev          cdev;
    struct device        *dev;

    struct tasklet_struct tsklt_slicer;

    char                 irq_name[12];
    unsigned int        vs_irq;
    spinlock_t            vbi_isr_lock;


    /* vbi memory */
    unsigned int         mem_start;
    unsigned int         mem_size;

    unsigned char        *pac_addr;
    unsigned char        *pac_addr_start;
    unsigned char        *pac_addr_end;
    unsigned int         current_pac_wptr;
    unsigned int         last_pac_wptr;
    unsigned int         vs_delay;

    struct vbi_slicer_s   *slicer;
    bool                   vbi_start;
    struct mutex          mutex;
    spinlock_t             lock;
	struct timer_list		timer;

} vbi_dev_t;

#endif /* TVIN_VBI_H_ */
