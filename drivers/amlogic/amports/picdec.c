/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 23/04/2014 13:26
 *
 *******************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/ge2d/ge2d_main.h>
#include <linux/amlogic/ge2d/ge2d.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/sched/rt.h>
#include <linux/platform_device.h>
#include <linux/amlogic/ge2d/ge2d_main.h>
#include <linux/amlogic/ge2d/ge2d.h>
//#include "picdec_log.h"
#include "picdec.h"
//#include <linux/ctype.h>

#include <linux/videodev2.h>
#include <media/videobuf-core.h>
#include <media/videobuf2-core.h>
#include <media/videobuf-dma-contig.h>
#include <media/videobuf-vmalloc.h>
#include <media/videobuf-dma-sg.h>
#include <media/videobuf-res.h>
//#include <linux/amlogic/picdecapi.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <linux/ctype.h>
#include <linux/of.h>

#include <linux/sizes.h>
#include <linux/dma-mapping.h>
#include <linux/of_fdt.h>
#include <linux/dma-contiguous.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif
/*class property info.*/
//#include "picdeccls.h"
static int debug_flag = 0;

#define NO_TASK_MODE

#define DEBUG
#ifdef  DEBUG
#define  AMLOG   1
#define LOG_LEVEL_VAR amlog_level_picdec
#define LOG_MASK_VAR amlog_mask_picdec
#endif


#define  	LOG_LEVEL_HIGH    		0x00f
#define	LOG_LEVEL_1				0x001
#define 	LOG_LEVEL_LOW			0x000

#define LOG_LEVEL_DESC \
"[0x00]LOW[0X01]LEVEL1[0xf]HIGH"	

#define  	LOG_MASK_INIT			0x001
#define	LOG_MASK_IOCTL			0x002
#define	LOG_MASK_HARDWARE		0x004
#define	LOG_MASK_CONFIG		0x008
#define	LOG_MASK_WORK			0x010
#define 	LOG_MASK_DESC \
"[0x01]:INIT,[0x02]:IOCTL,[0x04]:HARDWARE,[0x08]LOG_MASK_CONFIG[0x10]LOG_MASK_WORK"


static int task_running = 0;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define GE2D_NV
#endif


#define MAX_VF_POOL_SIZE 2

#define PIC_DEC_CANVAS_START 0
#define PIC_DEC_CANVAS_Y_FRONT PIC_DEC_CANVAS_START+1
#define PIC_DEC_CANVAS_UV_FRONT PIC_DEC_CANVAS_Y_FRONT+1

#define PIC_DEC_CANVAS_Y_BACK PIC_DEC_CANVAS_UV_FRONT+1
#define PIC_DEC_CANVAS_UV_BACK PIC_DEC_CANVAS_Y_BACK +1
#define PIC_DEC_SOURCE_CANVAS  PIC_DEC_CANVAS_UV_BACK +1


/*same as tvin pool*/
static int PICDEC_POOL_SIZE = 2 ;
static int VF_POOL_SIZE = 2;
/*same as tvin pool*/


static picdec_device_t  picdec_device;
static source_input_t   picdec_input;


#define INCPTR(p) ptr_atomic_wrap_inc(&p)




#define GE2D_ENDIAN_SHIFT        24
#define GE2D_ENDIAN_MASK            (0x1 << GE2D_ENDIAN_SHIFT)
#define GE2D_BIG_ENDIAN             (0 << GE2D_ENDIAN_SHIFT)
#define GE2D_LITTLE_ENDIAN          (1 << GE2D_ENDIAN_SHIFT)

#define PROVIDER_NAME "decoder.pic"
static DEFINE_SPINLOCK(lock);


static inline void ptr_atomic_wrap_inc(u32 *ptr)
{
	u32 i = *ptr;
	i++;
	if (i >= PICDEC_POOL_SIZE)
		i = 0;
	*ptr = i;
}




int start_picdec_task(void) ;
int start_picdec_simulate_task(void);



static struct vframe_s vfpool[MAX_VF_POOL_SIZE];
/*static u32 vfpool_idx[MAX_VF_POOL_SIZE];*/
static s32 vfbuf_use[MAX_VF_POOL_SIZE];
static s32 fill_ptr, get_ptr, putting_ptr, put_ptr;

struct semaphore  pic_vb_start_sema;
struct semaphore  pic_vb_done_sema;

static wait_queue_head_t frame_ready;


static int render_frame(ge2d_context_t *context,config_para_ex_t* ge2d_config);
static void post_frame(void);

/************************************************
*
*   buffer op for video sink.
*
*************************************************/

static int picdec_canvas_table[2];
static inline u32 index2canvas(u32 index)
{
	return picdec_canvas_table[index];
}

static vframe_t *picdec_vf_peek(void*);
static int picdec_vf_states(vframe_states_t *states, void* op_arg);
static vframe_t *picdec_vf_get(void*);
static void picdec_vf_put(vframe_t *vf, void*);
static int picdec_vf_states(vframe_states_t *states, void* op_arg)
{
	int i;
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);
    states->vf_pool_size = VF_POOL_SIZE;
    i = fill_ptr - get_ptr;
    if (i < 0) i += VF_POOL_SIZE;
    states->buf_avail_num = i;

    spin_unlock_irqrestore(&lock, flags);	
	return 0;
}

static vframe_t *picdec_vf_peek(void* op_arg)
{
	if (get_ptr == fill_ptr)
		return NULL;
	return &vfpool[get_ptr];
}

static vframe_t *picdec_vf_get(void* op_arg)
{
	vframe_t *vf;

	if (get_ptr == fill_ptr)
		return NULL;
	vf = &vfpool[get_ptr];
	INCPTR(get_ptr);
	return vf;
}

static void picdec_vf_put(vframe_t *vf, void* op_arg)
{
	int i;
	int  canvas_addr;
	if(!vf)
		return;
	INCPTR(putting_ptr);
	for (i = 0; i < VF_POOL_SIZE; i++) {
		canvas_addr = index2canvas(i);
		if(vf->canvas0Addr == canvas_addr ){
			vfbuf_use[i] = 0;
			printk("**********recycle buffer index : %d *************\n" , i);
		}
	}
}


static const struct vframe_operations_s picdec_vf_provider =
{
	.peek = picdec_vf_peek,
	.get  = picdec_vf_get,
	.put  = picdec_vf_put,
	.vf_states = picdec_vf_states,
};

static struct vframe_provider_s picdec_vf_prov;



int get_unused_picdec_index(void)
{
	int i;
	for (i = 0; i < VF_POOL_SIZE; i++){
		if(vfbuf_use[i] == 0)
			return i;
	}
	return -1;
}
static int render_frame(ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
	vframe_t* new_vf;
	int index;
	struct timeval start;
	struct timeval end;
	unsigned long time_use=0;
	do_gettimeofday(&start);
	index = get_unused_picdec_index();
	if(index < 0){
		printk("no buffer available, need post ASAP\n");
		//return -1;
		index = fill_ptr;
	}
	//printk("render buffer index is %d!!!!!\n", index);	
	new_vf = &vfpool[fill_ptr];
	new_vf->canvas0Addr =new_vf->canvas1Addr = index2canvas(index);
	new_vf->width = picdec_device.disp_width;
	new_vf->height = picdec_device.disp_height;
	new_vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VIU_NV21;
	new_vf->duration_pulldown = 0;
	new_vf->index = index;
	new_vf->pts = 0 ;
	new_vf->pts_us64 = 0;	
	new_vf->ratio_control = 0;
	vfbuf_use[index]++;
	//printk("picdec_fill_buffer start\n");
    picdec_fill_buffer(new_vf,context ,ge2d_config);
	//printk("picdec_fill_buffer finish\n"); 
	do_gettimeofday(&end);
	time_use = (end.tv_sec - start.tv_sec) * 1000+
			(end.tv_usec - start.tv_usec) / 1000;
	printk("render frame time use: %ldms\n", time_use);	
	return 0;
}


static int render_frame_block(void)
{
	vframe_t* new_vf;
	int index;
	struct timeval start;
	struct timeval end;
	unsigned long time_use=0;
	config_para_ex_t ge2d_config;		
	ge2d_context_t *context=picdec_device.context;
	do_gettimeofday(&start);
	memset(&ge2d_config,0,sizeof(config_para_ex_t));
	index = get_unused_picdec_index();
	if(index < 0){
		printk("no buffer available, need post ASAP\n");
		//return -1;
		index = fill_ptr;
	}
	//printk("render buffer index is %d$$$$$$$$$$$$$$$\n", index);		
	new_vf = &vfpool[fill_ptr];
	new_vf->canvas0Addr =new_vf->canvas1Addr = index2canvas(index);
	new_vf->width = picdec_device.disp_width;
	new_vf->height = picdec_device.disp_height;
	new_vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VIU_NV21;
	new_vf->duration_pulldown = 0;
	new_vf->index = index;
	new_vf->pts = 0 ;
	new_vf->pts_us64 = 0;	
	new_vf->ratio_control = 0;
	picdec_device.cur_index= index;
	
	//printk("picdec_fill_buffer start\n");
    picdec_fill_buffer(new_vf,context ,&ge2d_config);
	//printk("picdec_fill_buffer finish\n"); 
	//destroy_ge2d_work_queue(context);
	do_gettimeofday(&end);
	time_use = (end.tv_sec - start.tv_sec) * 1000+
			(end.tv_usec - start.tv_usec) / 1000;
	if(debug_flag){
		printk("Total render frame time use: %ldms\n", time_use);		
	}
	return 0;
}

static void post_frame(void)
{
	vfbuf_use[picdec_device.cur_index]++;
	INCPTR(fill_ptr);

	vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
}


/*************************************************
*
*   buffer op for decoder, camera, etc.
*
*************************************************/
/* static const vframe_provider_t *vfp = NULL; */

void picdec_local_init(void)
{
    int i;
    for(i = 0;i < MAX_VF_POOL_SIZE;i++)
    {
        vfbuf_use[i] = 0;
    }
    fill_ptr=get_ptr=putting_ptr=put_ptr=0;
    memset((char*)&picdec_input,0,sizeof(source_input_t));
    picdec_device.context = NULL;
    picdec_device.cur_index = 0;
    return;
}

static vframe_receiver_op_t* picdec_stop(void)
{
//    ulong flags;    

	vf_unreg_provider(&picdec_vf_prov);
	printk("vf_unreg_provider : picdec \n");
#ifndef NO_TASK_MODE	
	stop_picdec_task();
#else
	destroy_ge2d_work_queue(picdec_device.context);
#endif
    set_freerun_mode(0);
    picdec_local_init();

    if(picdec_device.mapping){
        io_mapping_free( picdec_device.mapping);
        picdec_device.mapping = 0;
    }
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 0);
#endif  
	printk("stop picdec task\n");	
    return (vframe_receiver_op_t*)NULL;
}


static int picdec_start(void)
{
    ulong flags;

	resource_size_t buf_start;
	unsigned int buf_size;
	unsigned map_start  , map_size = 0;
	picdec_buffer_init();
	get_picdec_buf_info(&buf_start,&buf_size, NULL);
    spin_lock_irqsave(&lock, flags);
    spin_unlock_irqrestore(&lock, flags);
	map_start = picdec_device.assit_buf_start ;
	map_size  = buf_size - (picdec_device.assit_buf_start - buf_start);
	if(map_size > 0x1800000){   //max size is 3840*2160*3 
		map_size = 0x1800000;
	}
    picdec_device.mapping = io_mapping_create_wc( map_start, map_size );
    if(picdec_device.mapping <= 0){ 
    	printk("mapping failed!!!!!!!!!!!!\n");
    	return -1;
	}else{
		printk("mapping addr is %x : %p , mapping size is %d \n " ,map_start ,picdec_device.mapping , map_size );
    }
    
	vf_provider_init(&picdec_vf_prov, PROVIDER_NAME ,&picdec_vf_provider, NULL);	
    vf_reg_provider(&picdec_vf_prov);

    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);
	set_freerun_mode(1);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("ge2d", 1);
#endif
	picdec_local_init();	
#ifndef NO_TASK_MODE		
    start_picdec_task();
#else
	picdec_device.context=create_ge2d_work_queue();
#endif      
#if 0   
    start_picdec_simulate_task();
#endif
    return 0;
}


static int test_r =0xff;
static int test_g =0xff;
static int test_b =0xff;
/************************************************
*
*   main task functions.
*
*************************************************/
static unsigned int print_ifmt=0;
module_param(print_ifmt, uint, 0644);
MODULE_PARM_DESC(print_ifmt, "print input format\n");

/* fill the RGB user buffer to physical buffer
*/
int picdec_pre_process(void)
{   
	struct io_mapping *mapping_wc;
	void __iomem * buffer_start;
	int i,j;
	//resource_size_t offset = picdec_device.assit_buf_start - picdec_device.buffer_start;
	unsigned dst_addr_off =0 ;
	char* p;
	char* q;
	char* ref;	
	int ret =0;
	int bp = ((picdec_input.frame_width + 0x1f)& ~0x1f)*3 ;
	struct timeval start;
	struct timeval end;
	unsigned long time_use=0;
	do_gettimeofday(&start);	
	get_picdec_buf_info(NULL, NULL, &mapping_wc);
	if(!mapping_wc){
		return -1;
	}
	buffer_start= io_mapping_map_atomic_wc( mapping_wc, 0);
	if(buffer_start == NULL) {
		printk(" assit buffer mapping error\n");
		return -1;
	}	
	printk("picdec_input width is %d , height is %d\n", picdec_input.frame_width ,picdec_input.frame_height);
	if(picdec_input.input == NULL){
		for(j =0 ; j <picdec_input.frame_height; j++ ){
			for(i = 0 ; i < picdec_input.frame_width*3 ; i+=3 ){
				dst_addr_off = i + bp*j ;
				*(char*)(buffer_start + dst_addr_off) = test_r ;
				*(char*)(buffer_start + dst_addr_off+1) = test_g ;
				*(char*)(buffer_start + dst_addr_off+2) = test_b ;						
			}		
		}
	}else{
		switch(picdec_input.format){
			case 0: 	//RGB
			p = (char*)buffer_start;
			q = (char*)picdec_input.input;
			printk("RGB user space address is %x################\n",(unsigned)q );
			for(j =0 ; j <picdec_input.frame_height; j++ ){
				ret = copy_from_user(p, (void __user *)q,picdec_input.frame_width*3);
				q += picdec_input.frame_width*3 ;
				p += bp; 	
			}	
			break;
			case 1:        //RGBA
			p = ref =  (char*)buffer_start;
			q = (char*)picdec_input.input;
			printk("RGBA user space address is %x################\n",(unsigned)q );
			for(j =0 ; j <picdec_input.frame_height; j++ ){	
				p = ref;			
				for(i = 0 ;i < picdec_input.frame_width ; i++){
					ret = copy_from_user(p, (void __user *)q,3);
					q +=4;
					p +=3;	
				}
				ref += bp; 	
			}	
			printk("RGBA copy finish #########################\n");		
			break;
			case 2:        //ARGB
			p = ref = (char*)buffer_start;
			q = (char*)picdec_input.input;
			printk("ARGB user space address is %x################\n",(unsigned)q );
			for(j =0 ; j <picdec_input.frame_height; j++ ){			
				p = ref;				
				for(i = 0 ;i < picdec_input.frame_width ; i++){
					ret = copy_from_user(p, (void __user *)(q+1),3);
					q +=4;
					p +=3;	
				}			
				ref += bp; 	 	
			}	
			printk("ARGB copy finish######################### \n");				
			break;
			default:
			p = (char*)buffer_start;
			q = (char*)picdec_input.input;
			printk("user space address is %x################\n",(unsigned)q );
			for(j =0 ; j <picdec_input.frame_height; j++ ){
				ret = copy_from_user(p, (void __user *)q,picdec_input.frame_width*3);
				q += picdec_input.frame_width*3 ;
				p += bp; 	
			}				
			break;				
		}
	}
	io_mapping_unmap_atomic( buffer_start );
	do_gettimeofday(&end);
	time_use = (end.tv_sec - start.tv_sec) * 1000+
			(end.tv_usec - start.tv_usec) / 1000;	
	if(debug_flag){
		printk("picdec_pre_process time use: %ldms\n", time_use);		
	}	
	return 0;
}

int fill_color(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
	struct io_mapping *mapping_wc;
	//void __iomem * buffer_start;
	canvas_t cs0,cs1,cs2;
	//canvas_t cd;
	//int dst_top, dst_left ,dst_width , dst_height;
	//int i,j;
	//resource_size_t offset = picdec_device.assit_buf_start - picdec_device.buffer_start;
	struct timeval start;
	struct timeval end;
	unsigned long time_use=0;
	void __iomem *p ;
	do_gettimeofday(&start);	
	get_picdec_buf_info(NULL, NULL, &mapping_wc);
	if(!mapping_wc){
		return -1;
	}
#if 0		
	buffer_start= io_mapping_map_atomic_wc( mapping_wc, 0);
	if(buffer_start == NULL) {
		printk(" assit buffer mapping error\n");
		return -1;
	}	
	memset((char*)buffer_start,0,640*480*3);
	canvas_config(PIC_DEC_SOURCE_CANVAS, 
	(ulong)(picdec_device.assit_buf_start),
	640*3, 480,
	CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);		
    ge2d_config->alu_const_color= 0;//0x000000ff;
	ge2d_config->bitmask_en  = 0;
	ge2d_config->src1_gb_alpha = 0;//0xff;
	ge2d_config->dst_xy_swap = 0;

	canvas_read(PIC_DEC_SOURCE_CANVAS&0xff,&cs0);
	canvas_read((PIC_DEC_SOURCE_CANVAS>>8)&0xff,&cs1);
	canvas_read((PIC_DEC_SOURCE_CANVAS>>16)&0xff,&cs2);
	ge2d_config->src_planes[0].addr = cs0.addr;
	ge2d_config->src_planes[0].w = cs0.width;
	ge2d_config->src_planes[0].h = cs0.height;
	ge2d_config->src_planes[1].addr = cs1.addr;
	ge2d_config->src_planes[1].w = cs1.width;
	ge2d_config->src_planes[1].h = cs1.height;
	ge2d_config->src_planes[2].addr = cs2.addr;
	ge2d_config->src_planes[2].w = cs2.width;
	ge2d_config->src_planes[2].h = cs2.height;

	ge2d_config->src_key.key_enable = 0;
	ge2d_config->src_key.key_mask = 0;
	ge2d_config->src_key.key_mode = 0;
	ge2d_config->src_para.canvas_index=PIC_DEC_SOURCE_CANVAS;
	ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->src_para.format = GE2D_FORMAT_S24_BGR;
	ge2d_config->src_para.fill_color_en = 0;
	ge2d_config->src_para.fill_mode = 0;
	ge2d_config->src_para.x_rev = 0;
	ge2d_config->src_para.y_rev = 0;
	ge2d_config->src_para.color = 0xffffffff;
	ge2d_config->src_para.top = 0;
	ge2d_config->src_para.left = 0;
	ge2d_config->src_para.width = 640;
	ge2d_config->src_para.height =480;
	ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
	
	ge2d_config->dst_para.canvas_index= vf->canvas0Addr;
	
	canvas_read(vf->canvas0Addr&0xff,&cs0);
	canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
	canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);	

	ge2d_config->dst_planes[0].addr = cs0.addr;
	ge2d_config->dst_planes[0].w = cs0.width;
	ge2d_config->dst_planes[0].h = cs0.height;
	ge2d_config->dst_planes[1].addr = cs1.addr;
	ge2d_config->dst_planes[1].w = cs1.width;
	ge2d_config->dst_planes[1].h = cs1.height;
	ge2d_config->dst_planes[2].addr = cs2.addr;
	ge2d_config->dst_planes[2].w = cs2.width;
	ge2d_config->dst_planes[2].h = cs2.height;	

	ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->dst_para.format = GE2D_FORMAT_M24_NV21;
	ge2d_config->dst_para.fill_color_en = 0;
	ge2d_config->dst_para.fill_mode = 0;
	ge2d_config->dst_para.x_rev = 0;
	ge2d_config->dst_para.y_rev = 0;
	ge2d_config->dst_para.color = 0;
	ge2d_config->dst_para.top = 0;
	ge2d_config->dst_para.left = 0;
	ge2d_config->dst_para.width = picdec_device.disp_width;
	ge2d_config->dst_para.height = picdec_device.disp_height;	

	if(ge2d_context_config_ex(context,ge2d_config)<0) {
		printk("++ge2d configing error.\n");
		return -1;
	}
	dst_top = 0;
	dst_left = 0 ;
	dst_width =  picdec_device.disp_width ;
	dst_height = picdec_device.disp_height ;

	stretchblt_noalpha(context,0 ,0 ,640, 480,dst_left,dst_top,dst_width,dst_height);	
	io_mapping_unmap_atomic( buffer_start );
#else
 	
	canvas_read(vf->canvas0Addr&0xff,&cs0);
	canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
	canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);		
    p = ioremap_nocache(cs0.addr, cs0.width*cs0.height);	
    memset(p,0,cs0.width*cs0.height);
    iounmap(p);
    p = ioremap_nocache(cs1.addr, cs1.width*cs1.height);	
    memset(p,0x80,cs1.width*cs1.height);
    iounmap(p);   
#endif	
	
	do_gettimeofday(&end);
	time_use = (end.tv_sec - start.tv_sec) * 1000+
			(end.tv_usec - start.tv_usec) / 1000;	
	if(debug_flag){
		printk("clear background time use: %ldms\n", time_use);		
	}	
	return 0;
	
#if 0	
	canvas_t cs0,cs1,cs2,cd;
    memset(ge2d_config,0,sizeof(config_para_ex_t));
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(vf->canvas0Addr&0xff,&cd);
    ge2d_config->src_planes[0].addr = cd.addr;
    ge2d_config->src_planes[0].w = cd.width;
    ge2d_config->src_planes[0].h = cd.height;
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;

    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;

    ge2d_config->src_para.canvas_index=vf->canvas0Addr&0xff;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = GE2D_FORMAT_S8_Y;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width =  vf->width;
    ge2d_config->src_para.height = vf->height;

    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;

    ge2d_config->dst_para.canvas_index= vf->canvas0Addr&0xff;
    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = GE2D_FORMAT_S8_Y;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = vf->width;
    ge2d_config->dst_para.height = vf->height;

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }
    fillrect(context, 0, 0, vf->width, vf->height, test_r);
    
    canvas_read((vf->canvas0Addr >>8)&0xff,&cd);
    ge2d_config->src_planes[0].addr = cd.addr;
    ge2d_config->src_planes[0].w = cd.width;
    ge2d_config->src_planes[0].h = cd.height;
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;  
    ge2d_config->src_para.canvas_index=(vf->canvas0Addr >> 8)&0xff;
    ge2d_config->dst_para.canvas_index= (vf->canvas0Addr >> 8)&0xff;
    ge2d_config->src_para.width =  vf->width;
    ge2d_config->src_para.height = vf->height/2;
    ge2d_config->dst_para.width = vf->width;
    ge2d_config->dst_para.height = vf->height/2;        
    ge2d_config->src_para.format = GE2D_FORMAT_S8_CB;
    ge2d_config->dst_para.format = GE2D_FORMAT_S8_CB;
    
    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        printk("++ge2d configing error.\n");
        return;
    }     
    fillrect(context, 0, 0, vf->width, vf->height/2, test_b);
    memset(ge2d_config,0,sizeof(config_para_ex_t));
 
 	return 0;   
#endif 	
}

static void rotate_adjust(int w_in ,int h_in , int* w_out, int* h_out, int angle)
{
    int w = 0, h = 0,disp_w = 0, disp_h =0;
    disp_w = picdec_device.disp_width;
    disp_h = picdec_device.disp_height;

    if ((angle == 90)||(angle == 270)){	
    	if(( w_in < disp_h)&&(h_in < disp_w)){    		
    		w = h_in;
    		h = w_in;	
    	}else{ 	
			h = min((int)w_in, disp_h);
			w = h_in * h / w_in;
	
			if(w > disp_w ){
				h = (h * disp_w)/w ;
				w = disp_w;
			}    		    	
    	}

    } else {
        if ((w_in < disp_w) && (h_in < disp_h)) {
            w = w_in;
            h = h_in;
        } else {
            if ((w_in * disp_h) > (disp_w * h_in)) {
                w = disp_w;
                h = disp_w * h_in / w_in;
            } else {
                h = disp_h;
                w = disp_h * w_in / h_in;
            }
        }
    }

    *w_out = w;
    *h_out = h;
}



int picdec_fill_buffer(vframe_t* vf, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
	canvas_t cs0,cs1,cs2;
	int canvas_id = PIC_DEC_SOURCE_CANVAS;
	int canvas_width =  (picdec_input.frame_width + 0x1f)& ~0x1f ;
	int canvas_height = (picdec_input.frame_height + 0xf)& ~0xf	;
    int frame_width = picdec_input.frame_width ;
    int frame_height = picdec_input.frame_height;
    int dst_top, dst_left ,dst_width , dst_height;
	fill_color(vf, context, ge2d_config);
	//memset(&ge2d_config,0,sizeof(config_para_ex_t));    
	canvas_config(PIC_DEC_SOURCE_CANVAS, 
	(ulong)(picdec_device.assit_buf_start),
	canvas_width*3, canvas_height,
	CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);	
	//printk("picdec_pre_process start %d\n");	
	picdec_pre_process();
	//printk("picdec_pre_process finish %d\n");	
    ge2d_config->alu_const_color= 0;//0x000000ff;
	ge2d_config->bitmask_en  = 0;
	ge2d_config->src1_gb_alpha = 0;//0xff;
	ge2d_config->dst_xy_swap = 0;

	canvas_read((canvas_id & 0xff),&cs0);
	canvas_read(((canvas_id >> 8) & 0xff),&cs1);
	canvas_read(((canvas_id >> 16) & 0xff),&cs2);
	ge2d_config->src_planes[0].addr = cs0.addr;
	ge2d_config->src_planes[0].w = cs0.width;
	ge2d_config->src_planes[0].h = cs0.height;
	ge2d_config->src_planes[1].addr = cs1.addr;
	ge2d_config->src_planes[1].w = cs1.width;
	ge2d_config->src_planes[1].h = cs1.height;
	ge2d_config->src_planes[2].addr = cs2.addr;
	ge2d_config->src_planes[2].w = cs2.width;
	ge2d_config->src_planes[2].h = cs2.height;
	//printk("src addr is %lx , plane 0 width is %d , height is %d\n",cs0.addr, cs0.width ,cs0.height );

	ge2d_config->src_key.key_enable = 0;
	ge2d_config->src_key.key_mask = 0;
	ge2d_config->src_key.key_mode = 0;
	ge2d_config->src_para.canvas_index=PIC_DEC_SOURCE_CANVAS;
	ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->src_para.format = GE2D_FORMAT_S24_BGR;
	ge2d_config->src_para.fill_color_en = 0;
	ge2d_config->src_para.fill_mode = 0;
	ge2d_config->src_para.x_rev = 0;
	ge2d_config->src_para.y_rev = 0;
	ge2d_config->src_para.color = 0xffffffff;
	ge2d_config->src_para.top = 0;
	ge2d_config->src_para.left = 0;
	ge2d_config->src_para.width = frame_width;
	ge2d_config->src_para.height =frame_height;
	/* printk("vf_width is %d , vf_height is %d \n",vf->width ,vf->height); */
	ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
	
	ge2d_config->dst_para.canvas_index= vf->canvas0Addr;
	
	canvas_read(vf->canvas0Addr&0xff,&cs0);
	canvas_read((vf->canvas0Addr>>8)&0xff,&cs1);
	canvas_read((vf->canvas0Addr>>16)&0xff,&cs2);	

	ge2d_config->dst_planes[0].addr = cs0.addr;
	ge2d_config->dst_planes[0].w = cs0.width;
	ge2d_config->dst_planes[0].h = cs0.height;
	ge2d_config->dst_planes[1].addr = cs1.addr;
	ge2d_config->dst_planes[1].w = cs1.width;
	ge2d_config->dst_planes[1].h = cs1.height;
	ge2d_config->dst_planes[2].addr = cs2.addr;
	ge2d_config->dst_planes[2].w = cs2.width;
	ge2d_config->dst_planes[2].h = cs2.height;	
	
	//printk("index is %ld ,dst addr is %lx , plane 0 width is %d , height is %d\n",vf->canvas0Addr ,cs0.addr, cs0.width ,cs0.height );
	ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
	ge2d_config->dst_para.format = GE2D_FORMAT_M24_NV21;
	ge2d_config->dst_para.fill_color_en = 0;
	ge2d_config->dst_para.fill_mode = 0;
	ge2d_config->dst_para.x_rev = 0;
	ge2d_config->dst_para.y_rev = 0;
	ge2d_config->dst_para.color = 0;
	ge2d_config->dst_para.top = 0;
	ge2d_config->dst_para.left = 0;
	ge2d_config->dst_para.width = picdec_device.disp_width;
	ge2d_config->dst_para.height = picdec_device.disp_height;	
	if(picdec_input.rotate==90){
		ge2d_config->dst_xy_swap = 1;
		ge2d_config->dst_para.x_rev ^= 1;
	}else if(picdec_input.rotate==180){
		ge2d_config->dst_para.x_rev ^= 1;
		ge2d_config->dst_para.y_rev ^= 1;
	}else if(picdec_input.rotate==270){
		ge2d_config->dst_xy_swap = 1;
		ge2d_config->dst_para.y_rev ^= 1;
	}else{	
		ge2d_config->dst_para.x_rev = 0;
		ge2d_config->dst_para.y_rev = 0;
	}

	if(ge2d_context_config_ex(context,ge2d_config)<0) {
		printk("++ge2d configing error.\n");
		return -1;
	}
	dst_top = 0;
	dst_left = 0 ;
	dst_width =  picdec_device.disp_width ;
	dst_height = picdec_device.disp_height ;
	
	rotate_adjust(frame_width ,frame_height,&dst_width ,&dst_height,picdec_input.rotate);
	
	dst_left = (picdec_device.disp_width -dst_width) >>1;	

	dst_top = (picdec_device.disp_height -dst_height) >>1;

	stretchblt_noalpha(context,0 ,0 ,frame_width, frame_height,dst_left,dst_top,dst_width,dst_height);	
	return 0;
}


static struct task_struct *task=NULL;
//static struct task_struct *simulate_task_fd=NULL;



/* static int reset_frame = 1; */
static int picdec_task(void *data) {
	int ret = 0;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };
	ge2d_context_t *context=create_ge2d_work_queue();
	config_para_ex_t ge2d_config;

#ifdef CONFIG_AMLCAP_LOG_TIME_USEFORFRAMES
	struct timeval start;
	struct timeval end;
	unsigned long time_use=0;
#endif

	memset(&ge2d_config,0,sizeof(config_para_ex_t));
	amlog_level(LOG_LEVEL_HIGH,"picdec task is running\n ");
	sched_setscheduler(current, SCHED_FIFO, &param);
	allow_signal(SIGTERM);
	while(1) {
		ret = down_interruptible(&pic_vb_start_sema);
		if (kthread_should_stop()){
			up(&pic_vb_done_sema);
			break;
		}
                  
		if(!task_running){
			up(&pic_vb_done_sema);
			goto picdec_exit;
			break;
		}
		render_frame(context,&ge2d_config);		
		
		if (kthread_should_stop()){
			up(&pic_vb_done_sema);
			break;
		}
		up(&pic_vb_done_sema);
	}
picdec_exit:
	destroy_ge2d_work_queue(context);
	while(!kthread_should_stop()){
	/* 	   may not call stop, wait..
                   it is killed by SIGTERM,eixt on down_interruptible
		   if not call stop,this thread may on do_exit and
		   kthread_stop may not work good;
	*/
		msleep(10);
	}
	return ret;
}

#if 0

/*simulate v4l2 device to request filling buffer,only for test use*/
static int simulate_task(void *data)
{
	while (1) {
		msleep(50);    
		picdec_fill_buffer(NULL,NULL,NULL);
		printk("simulate succeed\n");
	}
	return 0;
}

#endif
/************************************************
*

*   init functions.
*
*************************************************/
int picdec_buffer_init(void)
{
	int i;
	u32 canvas_width, canvas_height;
	u32 decbuf_size;
	resource_size_t buf_start;
	unsigned int buf_size;
    unsigned offset =0 ;

	get_picdec_buf_info(&buf_start,&buf_size, NULL);
	printk("picdec buffer size is %x\n", buf_size);
	sema_init(&pic_vb_start_sema,0);
	sema_init(&pic_vb_done_sema,0);

	if(!buf_start || !buf_size)
		goto exit;

    picdec_device.vinfo = get_current_vinfo();

    picdec_device.disp_width = picdec_device.vinfo->width;

    picdec_device.disp_height = picdec_device.vinfo->height;
	

	canvas_width = (picdec_device.disp_width  + 0x1f) & ~0x1f;
	canvas_height =(picdec_device.disp_height  + 0xf) & ~0xf; 
    decbuf_size =  canvas_width * canvas_height;
	for (i = 0; i < MAX_VF_POOL_SIZE; i++)
	{
		
		canvas_config(PIC_DEC_CANVAS_START + 2*i,
			(ulong)(buf_start + offset),
			canvas_width, canvas_height,
			CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
		offset += canvas_width * canvas_height;	
		canvas_config(PIC_DEC_CANVAS_START + 2*i +1,
			(ulong)(buf_start + offset),
			canvas_width, canvas_height/2,
			CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
		offset += canvas_width * canvas_height/2;				
		picdec_canvas_table[i] = (PIC_DEC_CANVAS_START + 2*i) | ((PIC_DEC_CANVAS_START + 2*i + 1)<<8);			
		vfbuf_use[i] = 0;
	}
	picdec_device.assit_buf_start = buf_start + canvas_width*canvas_height*3;
exit:
	return 0;

}




int start_picdec_task(void) {
	/* init the device. */

	if(!task) {
		task=kthread_create(picdec_task, &picdec_device,"picdec");
		if(IS_ERR(task)) {
			amlog_level(LOG_LEVEL_HIGH, "thread creating error.\n");
			return -1;
		}
		init_waitqueue_head(&frame_ready);
		wake_up_process(task);
	}
    task_running = 1;
    picdec_device.task_running = task_running;
	return 0;
}
#if 0
static int start_simulate_task(void)
{
	if(!simulate_task_fd) {
		simulate_task_fd=kthread_create(simulate_task,0,"picdec");
		if(IS_ERR(simulate_task_fd)) {
			amlog_level(LOG_LEVEL_HIGH, "thread creating error.\n");
			return -1;
		}
		wake_up_process(simulate_task_fd);
	}
	return 0;
}
#endif


void stop_picdec_task(void) {
    if(task){
        task_running = 0;
        picdec_device.task_running = task_running;
        send_sig(SIGTERM, task, 1);
        up(&pic_vb_start_sema);
        wake_up_interruptible(&frame_ready);
        kthread_stop(task);
        task = NULL;
    }
  
}


/***********************************************************************
*
* global status.
*
************************************************************************/

static int picdec_enable_flag=0;

int get_picdec_status(void) 
{
	return picdec_enable_flag;
}

void set_picdec_status(int flag) {
	if(flag >= 0)
		picdec_enable_flag=flag;
	else
		picdec_enable_flag=0;
}

/***********************************************************************
*
* file op section.
*
************************************************************************/

void set_picdec_buf_info(resource_size_t start,unsigned int size) {
	picdec_device.buffer_start=start;
	picdec_device.buffer_size=size;
	//picdec_device.mapping = io_mapping_create_wc( start, size );
	//printk("#############%p\n",picdec_device.mapping);
}


void unset_picdec_buf_info(void)
{
    if(picdec_device.mapping)
    {
        io_mapping_free( picdec_device.mapping);
        picdec_device.mapping = 0;
        picdec_device.buffer_start=0;
        picdec_device.buffer_size=0;
    }
}

void get_picdec_buf_info(resource_size_t* start,unsigned int* size,struct io_mapping **mapping) {
	if(start)
		*start = picdec_device.buffer_start;
	if(size)
		*size = picdec_device.buffer_size;
	if( mapping )
		*mapping = picdec_device.mapping;
}




static int picdec_open(struct inode *inode, struct file *file)
{
	 ge2d_context_t *context=NULL;
	 int ret = 0 ;
	 amlog_level(LOG_LEVEL_LOW,"open one picdec device\n");
	 file->private_data=context;
	 picdec_device.open_count++;
	 ret = picdec_start();
	 return ret;
}

static long picdec_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	int  ret=0 ;
	ge2d_context_t *context;
	void  __user* argp;	
	//source_input_t* input;
	context=(ge2d_context_t *)filp->private_data;
	argp =(void __user*)args;
	switch (cmd)
   	{
	case PICDEC_IOC_FRAME_RENDER:
		printk("PICDEC_IOC_FRAME_RENDER\n");
	    if(copy_from_user(&picdec_input, (void *)argp, sizeof(source_input_t))){	    
	       return -EFAULT;
	    }
#if 0
		picdec_input.input = input->input;
		picdec_input.frame_width     = input->frame_width;
        picdec_input.frame_height    = input->frame_height;
        picdec_input.format         = input->format;
        picdec_input.rotate         = input->rotate;
#endif        
		render_frame_block();
		break;
	case PICDEC_IOC_FRAME_POST:
	printk("PICDEC_IOC_FRAME_POST\n");
		post_frame();
		break;
	case PICDEC_IOC_CONFIG_FRAME:
		break;
	default :
		return -ENOIOCTLCMD;
	}
 	return ret;
}

static int picdec_release(struct inode *inode, struct file *file)
{
	ge2d_context_t *context=(ge2d_context_t *)file->private_data;
	printk("picdec stop start");
	picdec_stop();
	if(context && (0==destroy_ge2d_work_queue(context)))
	{
		picdec_device.open_count--;

		return 0;
	}
	printk("release one picdec device\n");
	return -1;
}

/***********************************************************************
*
* file op init section.
*
************************************************************************/

static const struct file_operations picdec_fops = {
	.owner = THIS_MODULE,
	.open = picdec_open,
	.unlocked_ioctl = picdec_ioctl,
	.release = picdec_release,
};

/*sys attribute*/
static int parse_para(const char *para, int para_num, int *result)
{
    char *endp;
    const char *startp = para;
    int *out = result;
    int len = 0, count = 0;

    if (!startp) {
        return 0;
    }

    len = strlen(startp);

    do {
        //filter space out
        while (startp && (isspace(*startp) || !isgraph(*startp)) && len) {
            startp++;
            len--;
        }

        if (len == 0) {
            break;
        }

        *out++ = simple_strtol(startp, &endp, 0);

        len -= endp - startp;
        startp = endp;
        count++;

    } while ((endp) && (count < para_num) && (len > 0));

    return count;
}


static ssize_t frame_render_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"render width :%d \nrender height:%d \nrender format: %d \n render direction: %d\n",picdec_input.frame_width,picdec_input.frame_height ,picdec_input.format,picdec_input.rotate);
}

static ssize_t frame_render_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;

    
    int parsed[4];
    
    if (likely(parse_para(buf, 4, parsed) == 4)) {
        picdec_input.frame_width     = parsed[0];
        picdec_input.frame_height    = parsed[1];
        picdec_input.format         = parsed[2];
        picdec_input.rotate         = parsed[3];
    }else{
    	return count ;
    }    
    picdec_input.input = NULL;
#ifdef NO_TASK_MODE
	render_frame_block();
#else    
    up(&pic_vb_start_sema);
#endif    
    size = endp - buf;
    return count;
}


static ssize_t frame_post_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"post command is %d\n",picdec_device.frame_post);
}

static ssize_t frame_post_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;

    picdec_device.frame_post = simple_strtoul(buf, &endp, 0);
    post_frame();
    size = endp - buf;
    return count;
}



static ssize_t test_color_read(struct class *cla,struct class_attribute *attr,char *buf)
{
    return snprintf(buf,80,"post command is %d\n",picdec_device.frame_post);
}

static ssize_t test_color_write(struct class *cla,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
    ssize_t size;
    char *endp;

    int parsed[3];
    
    if (likely(parse_para(buf, 3, parsed) == 3)) {
        test_r    = parsed[0];
        test_g   = parsed[1];
        test_b        = parsed[2];
    }else{
    	return count;
    }  
    size = endp - buf;
    return count;
}


static struct class_attribute picdec_class_attrs[] = {

    __ATTR(frame_render,
           S_IRUGO | S_IWUSR,
           frame_render_read,
           frame_render_write),
    __ATTR(frame_post,
           S_IRUGO | S_IWUSR,
           frame_post_read,
           frame_post_write),
    __ATTR(test_color,
           S_IRUGO | S_IWUSR,
           test_color_read,
           test_color_write),           
    __ATTR_NULL
};


#define PICDEC_CLASS_NAME   				"picdec"
static struct class picdec_class = {
	.name = PICDEC_CLASS_NAME,
	.class_attrs = picdec_class_attrs,
};

struct class* init_picdec_cls(void) {
	int  ret=0;
	ret = class_register(&picdec_class);
	if(ret < 0)
	{
		amlog_level(LOG_LEVEL_HIGH,"error create picdec class\r\n");
		return NULL;
	}
	return &picdec_class;
}



int init_picdec_device(void)
{
	int  ret=0;

	strcpy(picdec_device.name,"picdec");
	ret=register_chrdev(0,picdec_device.name,&picdec_fops);
	if(ret <=0)
	{
		amlog_level(LOG_LEVEL_HIGH,"register picdec device error\r\n");
		return  ret ;
	}
	picdec_device.major=ret;
	picdec_device.dbg_enable=0;
	amlog_level(LOG_LEVEL_LOW,"picdec_dev major:%d\r\n",ret);

	picdec_device.cla = init_picdec_cls();
	if(picdec_device.cla == NULL)
		return -1;
	picdec_device.dev=device_create(picdec_device.cla,NULL,MKDEV(picdec_device.major,0)
						,NULL,picdec_device.name);
	if (IS_ERR(picdec_device.dev)) {
		amlog_level(LOG_LEVEL_HIGH,"create picdec device error\n");
		goto unregister_dev;
	}

	//dump func
	//device_create_file( picdec_device.dev, &dev_attr_dump);
	picdec_device.dump = 0;

	dev_set_drvdata( picdec_device.dev,  &picdec_device);
	platform_set_drvdata( picdec_device.pdev,  &picdec_device);

	if(picdec_buffer_init()<0) goto unregister_dev;

	vf_provider_init(&picdec_vf_prov, PROVIDER_NAME ,&picdec_vf_provider, NULL);	

	return 0;

unregister_dev:
	class_unregister(picdec_device.cla);
	return -1;
}

int uninit_picdec_device(void)
{
	
	if(picdec_device.cla)
	{
		if(picdec_device.dev)
		device_destroy(picdec_device.cla, MKDEV(picdec_device.major, 0));
		picdec_device.dev = NULL;
		class_unregister(picdec_device.cla);
	}

	unregister_chrdev(picdec_device.major, picdec_device.name);
	return  0;
}



/*******************************************************************
 *
 * interface for Linux driver
 *
 * ******************************************************************/

MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0xff, LOG_LEVEL_DESC, LOG_MASK_DESC);

/* for driver. */
static int picdec_driver_probe(struct platform_device *pdev)
{

	char* buf_start;
	unsigned int buf_size;
	//struct resource *mem;
	struct device_node	*of_node = pdev->dev.of_node;
	const void *name;
    int idx;
    unsigned offset,size;
	idx = find_reserve_block(pdev->dev.of_node->name,0);
	
	
	if(idx < 0){
		name = of_get_property(of_node, "share-memory-name", NULL);
		if(!name){
			buf_start = 0;
			buf_size = 0;
			printk("picdec memory resource undefined.\n");
		}else{
			idx= find_reserve_block_by_name((char *)name);
			if(idx< 0)
			{
				printk("picdec share memory resource fail case 1.\n");
				return -EFAULT;
			}
			name = of_get_property(of_node, "share-memory-offset", NULL);
			if(name)
			   offset = of_read_ulong(name,1);
			else
			{
			    printk("picdec share memory resource fail case 2.\n");
				return -EFAULT;
			}
			name = of_get_property(of_node, "share-memory-size", NULL);
			if(name)
			size = of_read_ulong(name,1);
			else
			{
			    printk("picdec share memory resource fail case 3.\n");
				return -EFAULT;
			}
			
			
			buf_start = (char*)((phys_addr_t)get_reserve_block_addr(idx)+offset);
			buf_size =  size;
		}
	}
	else
	{
		 buf_start = (char *)((phys_addr_t)get_reserve_block_addr(idx));
		 buf_size = (unsigned int)get_reserve_block_size(idx);
	}

	set_picdec_buf_info((resource_size_t)buf_start,buf_size);
	picdec_device.mapping = 0; 
	picdec_device.pdev = pdev;
	init_picdec_device();
	return 0;
}

static int picdec_drv_remove(struct platform_device *plat_dev)
{
	uninit_picdec_device();

	if(picdec_device.mapping){
		io_mapping_free( picdec_device.mapping);
	}
	return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_picdec_dt_match[]={
	{	.compatible = "amlogic,picdec",
	},
	{},
};
#else
#define amlogic_picdec_dt_match NULL
#endif

/* general interface for a linux driver .*/
static struct platform_driver picdec_drv = {
	.probe  = picdec_driver_probe,
	.remove = picdec_drv_remove,
	.driver = {
		.name = "picdec",
		.owner = THIS_MODULE,
		.of_match_table = amlogic_picdec_dt_match,
	}
};

static int __init
picdec_init_module(void)
{
	int err;

	amlog_level(LOG_LEVEL_HIGH,"picdec_init\n");
	if ((err = platform_driver_register(&picdec_drv))) {
		printk(KERN_ERR "Failed to register picdec driver (error=%d\n", err);
		return err;
	}

	return err;
}

static void __exit
picdec_remove_module(void)
{
	platform_driver_unregister(&picdec_drv);
	amlog_level(LOG_LEVEL_HIGH,"picdec module removed.\n");
}

module_init(picdec_init_module);
module_exit(picdec_remove_module);
MODULE_PARM_DESC(debug_flag, "\n debug_flag\n");
module_param(debug_flag, uint, 0664);

MODULE_DESCRIPTION("Amlogic picture decoder driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Zheng <simon.zheng@amlogic.com>");
