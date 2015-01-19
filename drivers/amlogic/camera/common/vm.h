#ifndef _VM_INCLUDE__
#define _VM_INCLUDE__

#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/sysfs.h>
#include  <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/io-mapping.h>

#include <linux/amlogic/camera/aml_cam_info.h>

/**************************************************************
**																	 **
**	macro define		 												 **
**																	 **
***************************************************************/

#define VM_IOC_MAGIC  'P'
#define VM_IOC_2OSD0		_IOW(VM_IOC_MAGIC, 0x00, unsigned int)
#define VM_IOC_ENABLE_PP _IOW(VM_IOC_MAGIC,0X01,unsigned int)
#define VM_IOC_CONFIG_FRAME  _IOW(VM_IOC_MAGIC,0X02,unsigned int)

typedef  struct vm_device_s{
	char  			name[20];
        struct platform_device  *pdev;
        int                     task_running;
        int                     dump;
        char                    *dump_path;
	unsigned int 		open_count;
	int	 		major;
	unsigned  int 		dbg_enable;
	struct class 		*cla;
	struct device		*dev;
	resource_size_t buffer_start;
	unsigned int buffer_size;
	struct io_mapping *mapping;
}vm_device_t;

typedef struct display_frame_s{
	int frame_top;
	int frame_left;
	int frame_width;
	int frame_height;	
	int content_top;
	int content_left;
	int content_width;
	int content_height;
}display_frame_t;

extern int get_vm_status(void);
extern void set_vm_status(int flag);


/* for vm device op. */
extern int  init_vm_device(void);
extern int uninit_vm_device(void);

/* for vm device class op. */
extern struct class* init_vm_cls(void);

/* for thread of vm. */
extern int start_vpp_task(void);
extern void stop_vpp_task(void);

/* for vm private member. */
extern void set_vm_buf_info(resource_size_t start,unsigned int size);
extern void get_vm_buf_info(resource_size_t* start,unsigned int* size,struct io_mapping **mapping);


/*  vm buffer op. */
extern int vm_buffer_init(void);
extern void vm_local_init(void) ;
static DEFINE_MUTEX(vm_mutex);


#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
#define CANVAS_WIDTH_ALIGN 32
#else
#define CANVAS_WIDTH_ALIGN 8
#endif

#define MAGIC_SG_MEM    0x17890714
#define MAGIC_DC_MEM    0x0733ac61
#define MAGIC_VMAL_MEM  0x18221223
#define MAGIC_RE_MEM    0x123039dc

#endif /* _VM_INCLUDE__ */
