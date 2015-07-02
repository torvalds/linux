#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_fdt.h>



#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/canvas.h>
#include <mach/am_regs.h>

#include "d2d3_drv.h"
#include "../deinterlace/deinterlace.h"
#define D2D3_NAME               "d2d3"
#define D2D3_DRIVER_NAME        "d2d3"
#define D2D3_MODULE_NAME        "d2d3"
#define D2D3_DEVICE_NAME        "d2d3"
#define D2D3_CLASS_NAME         "d2d3"

#define VFM_NAME        "d2d3"

#define D2D3_COUNT 1

static d2d3_dev_t *d2d3_devp;
static struct vframe_provider_s * prov = NULL;
static dev_t d2d3_devno;
static struct class *d2d3_clsp;
struct semaphore thread_sem;

static bool d2d3_dbg_en = 0;
module_param(d2d3_dbg_en, bool, 0664);
MODULE_PARM_DESC(d2d3_dbg_en, "\n d2d3_dbg_en\n");

static unsigned long post_count = 0;
module_param(post_count,ulong,0644);
MODULE_PARM_DESC(post_count,"count the irq");

static unsigned long pre_count = 0;
module_param(pre_count,ulong,0644);
MODULE_PARM_DESC(pre_count,"count the di pre");

static short depth = 0;
module_param(depth,short,0644);
MODULE_PARM_DESC(depth,"\n the depth of field\n");


/*****************************
 * d2d3 processing mode
 * mode0:    DE_PRE-->DPG-->Memory
 * mode0:   DE_POST-->DBG-->VPP_SCALER
 *
 * mode1:    DE_PRE-->DPG-->Memory
 * mode1:VPP_SCALER-->DBG-->VPP_BLENDING
 *
 * mode2:   DE_POST-->DPG-->Memory
 * mode2:   DE_POST-->DBG-->VPP_SCALER
 *
 * mode3:VPP_SCALER-->DPG-->Memory
 * mode3:VPP_SCALER-->DBG-->VPP_BLENDING
 ******************************/

static int d2d3_receiver_event_fun(int type, void* data, void* arg);

static const struct vframe_receiver_op_s d2d3_vf_receiver =
{
        .event_cb = d2d3_receiver_event_fun
};

static struct vframe_receiver_s d2d3_vf_recv;

static vframe_t *d2d3_vf_peek(void* arg);
static vframe_t *d2d3_vf_get(void* arg);
static void d2d3_vf_put(vframe_t *vf, void* arg);
static int d2d3_event_cb(int type, void *data, void *private_data);

static const struct vframe_operations_s d2d3_vf_provider =
{
        .peek = d2d3_vf_peek,
        .get  = d2d3_vf_get,
        .put  = d2d3_vf_put,
        .event_cb = d2d3_event_cb,
};

static struct vframe_provider_s d2d3_vf_prov;

/*****************************
 *    d2d3 process :
 ******************************/
#define D2D3_IDX_MAX  5
typedef struct{
        int (*pre_early_process_fun)(void* arg, vframe_t* vf);
        int (*pre_process_fun)(void* arg, unsigned zoom_start_x_lines,
                        unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, vframe_t* vf);
        void* pre_private_data;
        vframe_t* vf;
        /*etc*/
}d2d3_devnox_t;
d2d3_devnox_t  d2d3_devnox[D2D3_IDX_MAX];

static unsigned char have_process_fun_private_data = 0;

static int d2d3_early_process_fun(void* arg, vframe_t* disp_vf)
{//return 1, make video set video_property_changed
        /* arg is vf->private_data */
        int ret = 0;
        int idx = (int)arg;
        if(idx>=0 && idx<D2D3_IDX_MAX){
                d2d3_devnox_t* p_d2d3_devnox = &d2d3_devnox[idx];
                if(have_process_fun_private_data && p_d2d3_devnox->pre_early_process_fun){
                        ret = p_d2d3_devnox->pre_early_process_fun(p_d2d3_devnox->pre_private_data, disp_vf);
                }
        }
        return ret;
}
/*
 *check the input&output format
 */
static int d2d3_update_param(struct d2d3_param_s *parm)
{
        bool config_flag = false;
        struct d2d3_param_s *local_parm = &d2d3_devp->param;

        if((parm->input_w != local_parm->input_w)||(parm->input_h != local_parm->input_h))
        {
                pr_info("[d2d3]%s: input fmt changed from %ux%u to %ux%u.\n",__func__,
                                local_parm->input_w, local_parm->input_h, parm->input_w, parm->input_h);
                local_parm->input_w  = parm->input_w;
                local_parm->input_h= parm->input_h;
                config_flag = true;
        }
        if((parm->output_w != local_parm->output_w)||(parm->output_h != local_parm->output_h))
        {
                pr_info("[d2d3]%s: output fmt changed from %ux%u to %ux%u.\n",__func__,
                                local_parm->output_w,local_parm->output_h, parm->output_w, parm->output_h);

                local_parm->output_w = parm->output_w;
                local_parm->output_h = parm->output_h;
                config_flag = true;
        }
        if(parm->reverse_flag != local_parm->reverse_flag){
                pr_info("[d2d3]%s: reverse flag changed from %u to %u.\n",__func__,
                         local_parm->reverse_flag, parm->reverse_flag);
                local_parm->reverse_flag = parm->reverse_flag;
                d2d3_canvas_reverse(local_parm->reverse_flag);
        }
        if(depth != local_parm->depth){
                if(d2d3_depth_adjust(depth)){
			depth = local_parm->depth;
		}
                else
                        local_parm->depth = depth;
        }
        if(config_flag){
                d2d3_config(d2d3_devp,local_parm);
		return 1;
        }
        return 0;
}

static int d2d3_irq_process(unsigned int input_w, unsigned int input_h,unsigned int reverse_flag)
{
        struct d2d3_param_s parm;
	int ret = 0;
        parm.input_w = input_w;
        parm.input_h = input_h;
        parm.reverse_flag = reverse_flag;
        get_real_display_size(&parm.output_w,&parm.output_h);
        ret = d2d3_update_param(&parm);
        if( D2D3_DPG_MUX_NRW != d2d3_devp->param.dpg_path){
                d2d3_update_canvas(d2d3_devp);
        }else{
                /*just update the dbr canvas config with the addr from di*/
                d2d3_config_dbr_canvas(d2d3_devp);
        }

        return ret;
}
static int d2d3_post_process_fun(void* arg, unsigned zoom_start_x_lines,
                unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, vframe_t* disp_vf)
{
        int ret = 0;
        int idx = (int)arg;
        d2d3_devnox_t* p_d2d3_devnox = NULL;
        di_buf_t *di_buf_p = NULL;
        if((idx>=0) && (idx<D2D3_IDX_MAX)){
                p_d2d3_devnox = &d2d3_devnox[idx];
                if(have_process_fun_private_data && p_d2d3_devnox->pre_process_fun){
                        ret = p_d2d3_devnox->pre_process_fun(p_d2d3_devnox->pre_private_data, zoom_start_x_lines, zoom_end_x_lines, zoom_start_y_lines, zoom_end_y_lines, disp_vf);
                }
        }else{
                pr_info("[d2d3]%s: the index %d over the max index.\n",__func__,idx);
        }
        post_count++;
        zoom_start_x_lines = zoom_start_x_lines&0xffff;
        zoom_end_x_lines   = zoom_end_x_lines&0xffff;
        zoom_start_y_lines = zoom_start_y_lines&0xffff;
        zoom_end_y_lines   = zoom_end_y_lines&0xffff;
        if(d2d3_devp->flag & D2D3_REG){
        	vf_notify_receiver(VFM_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                /*d2d3 irq process*/
                if(!(d2d3_devp->flag & D2D3_BYPASS)){
                        di_buf_p = p_d2d3_devnox->pre_private_data;
                        d2d3_devp->dbr_addr = di_buf_p->dp_buf_adr;
                        ret = d2d3_irq_process(zoom_end_x_lines-zoom_start_x_lines+1,
                                zoom_end_y_lines-zoom_start_y_lines+1,di_buf_p->reverse_flag);
                }
        }
        return ret;
}

/*
 *    d2d3 vfm interface
 */

static vframe_t *d2d3_vf_peek(void* arg)
{
        vframe_t* vframe_ret = vf_peek(d2d3_devp->vfm_name);
        return vframe_ret;
}

static vframe_t *d2d3_vf_get(void* arg)
{
        vframe_t* vframe_ret = NULL;
        int i;
        vframe_ret = vf_get(d2d3_devp->vfm_name);
        if(vframe_ret){
                for(i=0; i<D2D3_IDX_MAX; i++){
                        if(d2d3_devnox[i].vf == NULL){
                                break;
                        }
                }
                if(i==D2D3_IDX_MAX){
                        printk("[d2d3]%s:D2D3 Error, idx is not enough.\n",__func__);
                }
                else{
                        d2d3_devnox[i].vf = vframe_ret;
                        /* backup early_process_fun/process_fun/private_data */
                        d2d3_devnox[i].pre_early_process_fun = vframe_ret->early_process_fun;
                        d2d3_devnox[i].pre_process_fun = vframe_ret->process_fun;
                        d2d3_devnox[i].pre_private_data = vframe_ret->private_data;

                        vframe_ret->early_process_fun = d2d3_early_process_fun;
                        vframe_ret->process_fun = d2d3_post_process_fun;
                        vframe_ret->private_data = (void*)i;

                        /* d2d3 process code start*/

                        /*d2d3 process code*/
                        if(d2d3_dbg_en)
                               printk("[d2d3]%s: %d 0x%x.\n", __func__, i, (unsigned int)vframe_ret);
                }
        }
        return vframe_ret;
}

static void d2d3_vf_put(vframe_t *vf, void* arg)
{

        int idx = (int)(vf->private_data);
        //printk("%s %d\n", __func__,idx);
        /* d2d3 process code start*/

        /*d2d3 process code*/
        if((idx<D2D3_IDX_MAX)&&(idx>=0)){
                d2d3_devnox[idx].vf = NULL;
                /* restore early_process_fun/process_fun/private_data */
                vf->early_process_fun = d2d3_devnox[idx].pre_early_process_fun;
                vf->process_fun = d2d3_devnox[idx].pre_process_fun;
                vf->private_data = d2d3_devnox[idx].pre_private_data;
        } else {
                printk("[d2d3]%s: error, return vf->private_data %x is not in the "\
                                        "range.\n", __func__,idx);
        }

        prov = vf_get_provider(d2d3_devp->vfm_name);

	if(prov&&prov->ops&&prov->ops->put){
		prov->ops->put(vf,prov->op_arg);
		vf_notify_provider(d2d3_devp->vfm_name,VFRAME_EVENT_RECEIVER_PUT,prov->op_arg);
	}
}

static int d2d3_event_cb(int type, void *data, void *private_data)
{
        return 0;
}
static int d2d3_receiver_event_fun(int type, void* data, void* arg)
{
        int i, ret=0;
        d2d3_param_t *parm = &d2d3_devp->param;
        if((type == VFRAME_EVENT_PROVIDER_UNREG)||
           (type == VFRAME_EVENT_PROVIDER_LIGHT_UNREG)){
                prov = NULL;
		if(d2d3_devp->flag & D2D3_BYPASS){
                	d2d3_enable_hw(false);
			d2d3_enable_path(false, parm);
		}

                vf_notify_receiver(d2d3_devp->vfm_name,VFRAME_EVENT_PROVIDER_UNREG,NULL);
                vf_unreg_provider(&d2d3_vf_prov);
                d2d3_devp->flag &= (~D2D3_REG); // keep flag when unreg
                pr_info("[d2d3]%s: provider unregister,disable d2d3.\n",__func__);

        }
        else if(type == VFRAME_EVENT_PROVIDER_REG){
                char* provider_name = (char*)data;

                if((strcmp(provider_name, "deinterlace")==0)||(strcmp(provider_name, "ppmgr")==0)){
                        have_process_fun_private_data = 1;
                }
                else{
                        have_process_fun_private_data = 0;
                }
                vf_reg_provider(&d2d3_vf_prov);

                for(i=0; i<D2D3_IDX_MAX; i++){
                        d2d3_devnox[i].vf = NULL;
                }
                prov = vf_get_provider(d2d3_devp->vfm_name);
		d2d3_devp->flag |= D2D3_REG;
                vf_notify_receiver(d2d3_devp->vfm_name,VFRAME_EVENT_PROVIDER_START,NULL);
        }
        else if((VFRAME_EVENT_PROVIDER_DPBUF_CONFIG == type) &&
                        (D2D3_DPG_MUX_NRW == parm->dpg_path))
        {
                vframe_t * vf = (vframe_t*)data;
                struct di_buf_s *di_buf = vf->private_data;
                d2d3_devp->dpg_addr = di_buf->dp_buf_adr;
                /*just update the dpg canvas config with the addr from di*/
                d2d3_config_dpg_canvas(d2d3_devp);
                pre_count++;

        }
        else if(VFRAME_EVENT_PROVIDER_FR_HINT == type)
        {
               vf_notify_receiver(d2d3_devp->vfm_name,VFRAME_EVENT_PROVIDER_FR_HINT,data);
        }
        else if(VFRAME_EVENT_PROVIDER_FR_END_HINT == type)
        {
               vf_notify_receiver(d2d3_devp->vfm_name,VFRAME_EVENT_PROVIDER_FR_END_HINT,data);
        }
        return ret;
}



/*****************************
 *    d2d3 driver file_operations
 *
 ******************************/
static struct platform_device* d2d3_platform_device = NULL;

static int d2d3_open(struct inode *node, struct file *file)
{
        d2d3_dev_t *d2d3_devp;

        /* Get the per-device structure that contains this cdev */
        d2d3_devp = container_of(node->i_cdev, d2d3_dev_t, cdev);
        file->private_data = d2d3_devp;
        return 0;
}


static int d2d3_release(struct inode *node, struct file *file)
{
        file->private_data = NULL;
        return 0;
}

static int d2d3_add_cdev(struct cdev *cdevp, const struct file_operations *file_ops,int minor)
{
        int ret = 0;
        dev_t devno = MKDEV(MAJOR(d2d3_devno),minor);
        cdev_init(cdevp,file_ops);
        cdevp->owner = THIS_MODULE;
        ret = cdev_add(cdevp,devno,1);
        return ret;
}
static struct device *d2d3_create_device(struct device *parent, int min)
{
        dev_t devno = MKDEV(MAJOR(d2d3_devno),min);
        return device_create(d2d3_clsp,parent,devno,NULL, D2D3_DEVICE_NAME);
}
const static struct file_operations d2d3_fops = {
        .owner    = THIS_MODULE,
        .open     = d2d3_open,
        .release  = d2d3_release,
};

/*
 *  1.bypass d2d3 mode:echo bypass >/sys/class/d2d3/d2d3/debug
 *  2.switch the dpg,dbr path,dbr output mode such as line interleave
 *      echo swmode dpg_path dbr_path dbr_mode >/sys/class/d2d3/d2d3/debug
 */
static ssize_t store_dbg(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
        unsigned int n=0;
        char *buf_orig, *ps, *token;
        struct d2d3_dev_s *devp;
        struct d2d3_param_s *parm;
        char *parm_str[5];
        buf_orig = kstrdup(buf, GFP_KERNEL);
        //pr_info("[d2d3]%s:input cmd: %s",__func__,buf_orig);
        ps = buf_orig;
        devp = dev_get_drvdata(dev);
        parm = &devp->param;
        while (1) {
                token = strsep(&ps, " \n");
                if (token == NULL)
                        break;
                if (*token == '\0')
                        continue;
                parm_str[n++] = token;
        }
        if(!strcmp(parm_str[0],"swmode")){
                unsigned short dpg_path = 5, dbr_path = 2;
                unsigned short dbr_mode = 0;
                dpg_path  = simple_strtoul(parm_str[1],NULL,10);
                dbr_path  = simple_strtoul(parm_str[2],NULL,10);
                dbr_mode = simple_strtol(parm_str[3],NULL,10);
                /*load the default setting*/
                d2d3_set_def_config(parm);
                parm->dpg_path  = dpg_path;
                parm->dbr_path   = dbr_path;
                parm->dbr_mode = dbr_mode;
                parm->input_w  = 0;
                parm->input_h  = 0;
                /*get the real vpp display size*/
                get_real_display_size(&parm->output_w,&parm->output_h);
                pr_info("[d2d3]%s: hw start: dpg path %u, dbr path %u,dbr mode %u.\n",__func__,
                                devp->param.dpg_path, devp->param.dbr_path,devp->param.dbr_mode);
        }else if(!strcmp(parm_str[0],"bypass")){
		devp->flag |= D2D3_BYPASS;
		d2d3_enable_path(false,parm);
                d2d3_enable_hw(false);
        }else if(!strcmp(parm_str[0],"enable")){
	        if(devp->flag & D2D3_BYPASS) {
                        d2d3_set_def_config(parm);
                        d2d3_canvas_init(devp);
                        d2d3_enable_hw(true);
	                d2d3_enable_path(true,parm);
                        devp->flag &= (~D2D3_BYPASS);
                }
        }

        kfree(buf_orig);
        return count;
}
static ssize_t show_dbg(struct device * dev, struct device_attribute *attr, char * buf)
{
        struct d2d3_dev_s *devp = dev_get_drvdata(dev);
        ssize_t len = 0;
        len += sprintf(buf,"  1.the current dpg path %u, dbr path %u,dbr mode %u,%s bypass.\n"
                        "  2.set depth:echo x >/sys/module/d2d3/parameters/depth"
                        "  x's range[2~128].\n  3.get depth:cat /sys/module/d2d3/parameters/depth.\n",
                        devp->param.dpg_path,devp->param.dbr_path,devp->param.dbr_mode,
                        (devp->flag&D2D3_BYPASS)?"enable":"disable");
        return len;
}

static DEVICE_ATTR(debug, S_IWUGO | S_IRUGO, show_dbg, store_dbg);

static struct resource memobj;

static int d2d3_probe(struct platform_device *pdev)
{
        int ret = 0;
        struct d2d3_dev_s *devp;
        struct resource *res;

        /* kmalloc d2d3 dev */
        devp = kmalloc(sizeof(struct d2d3_dev_s), GFP_KERNEL);
        if (!devp)
        {
                printk("[d2d3]%s: failed to allocate memory for d2d3 device.\n",__func__);
                ret = -ENOMEM;
                goto failed_kmalloc_devp;
        }
        memset(devp,0,sizeof(d2d3_dev_t));
        d2d3_devp = devp;
        //spin_lock_init(&d2d3_devp->buf_lock);
        d2d3_devp->flag = D2D3_BYPASS;
	/*disable hw&path*/
	d2d3_enable_hw(false);
	d2d3_enable_path(false,&d2d3_devp->param);
        /*create cdev and register with sysfs*/
        ret = d2d3_add_cdev(&devp->cdev, &d2d3_fops,0);
        if (ret) {
                printk("[d2d3]%s: failed to add device.\n",__func__);
                /* @todo do with error */
                goto failed_add_cdev;
        }
        /*create the udev node /dev/...*/
        devp->dev = d2d3_create_device(&pdev->dev,0);
        if (devp->dev == NULL) {
                printk("[d2d3]%s: device_create create error.\n",__func__);
                ret = -EEXIST;
                goto failed_create_device;
        }
        ret = device_create_file(devp->dev, &dev_attr_debug);
        if(ret < 0){
                printk(KERN_ERR "[d2d3]%s: failed create device attr file.\n",__func__);
                goto failed_create_device_file;
        }
      #ifdef CONFIG_USE_OF
      	res = &memobj;
       	ret = find_reserve_block(pdev->dev.of_node->name,0);
      	if(ret < 0){
             pr_err("\nd2d3 memory resource undefined.\n");
             return -EFAULT;
     	}
      	printk("[d2d3]%s: hhhhhhhhhhhhhh[%d].\n",__func__,__LINE__);
      	res->start = (phys_addr_t)get_reserve_block_addr(ret);
      	res->end = res->start+ (phys_addr_t)get_reserve_block_size(ret)-1;
      #else
        /* get device memory */
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res) {
                pr_err("[d2d3]%s: can't get memory resource.\n",__func__);
                ret = -EFAULT;
                goto failed_get_resource;
        }
	#endif
        devp->mem_start = res->start;
        devp->mem_size  = res->end - res->start + 1;
        printk("[d2d3]%s: mem_start = 0x%x, mem_size = 0x%x.\n",__func__, devp->mem_start,devp->mem_size);
#ifdef CONFIG_USE_OF
     	if (pdev->dev.of_node) {
               	ret = of_property_read_u32(pdev->dev.of_node,"irq",&(res->start));
              	if(ret) {
                       pr_err("don't find d2d3  match irq\n");
                     	goto failed_get_resource;
             	}
              res->end = res->start;
              res->flags = IORESOURCE_IRQ;
      	}
#else
	/*get d2d3's irq*/
        res = platform_get_resource(pdev,IORESOURCE_MEM,0);
        if (!res) {
                pr_err("[d2d3]%s: can't get irq resource.\n",__func__);
                ret = -EFAULT;
                goto failed_get_resource;
        }
#endif
        devp->irq = res->start;
        sprintf(devp->vfm_name,"%s",VFM_NAME);
        platform_set_drvdata(pdev,(void *)devp);
        dev_set_drvdata(devp->dev,(void *)devp);

        vf_receiver_init(&d2d3_vf_recv, devp->vfm_name, &d2d3_vf_receiver, NULL);
        vf_reg_receiver(&d2d3_vf_recv);

        vf_provider_init(&d2d3_vf_prov, devp->vfm_name, &d2d3_vf_provider, NULL);
        return ret;
failed_get_resource:
        device_remove_file(&pdev->dev,&dev_attr_debug);
failed_create_device_file:
        device_del(&pdev->dev);
failed_create_device:
        cdev_del(&devp->cdev);
failed_add_cdev:
        kfree(devp);
failed_kmalloc_devp:
        return ret;
}

static int d2d3_remove(struct platform_device *pdev)
{
        vf_unreg_provider(&d2d3_vf_prov);
        vf_unreg_receiver(&d2d3_vf_recv);

        /* Remove the cdev */
        device_remove_file(d2d3_devp->dev, &dev_attr_debug);

        cdev_del(&d2d3_devp->cdev);
        kfree(d2d3_devp);
        d2d3_devp = NULL;
        device_del(d2d3_devp->dev);
        return 0;
}

static int d2d3_suspend(struct platform_device *pdev, pm_message_t state)
{
        struct d2d3_dev_s *d2d3_devp;
        d2d3_devp = platform_get_drvdata(pdev);
        d2d3_enable_hw(false);
		d2d3_enable_path(false,&d2d3_devp->param);
        return 0;
}

static int d2d3_resume(struct platform_device *pdev)
{
        struct d2d3_dev_s *d2d3_devp;
        d2d3_devp = platform_get_drvdata(pdev);
	if(d2d3_devp->flag & D2D3_BYPASS){
        	d2d3_enable_hw(false);
		d2d3_enable_path(false,&d2d3_devp->param);
	} else {
        	d2d3_enable_hw(true);
		d2d3_enable_path(true,&d2d3_devp->param);
	}
        return 0;
}
#ifdef CONFIG_OF
static const struct of_device_id d2d3_dt_match[]={
    {
        .compatible     = "amlogic,d2d3",
    },
    {},
};
#else
#define d2d3_dt_match NULL
#endif



static struct platform_driver d2d3_driver = {
        .probe      = d2d3_probe,
        .remove     = d2d3_remove,
        .suspend    = d2d3_suspend,
        .resume     = d2d3_resume,
        .driver     = {
                .name   = D2D3_DEVICE_NAME,
                .owner  = THIS_MODULE,
                .of_match_table = d2d3_dt_match,
        }
};

static int  __init d2d3_drv_init(void)
{
        int ret = 0;
#if 0
        d2d3_platform_device = platform_device_alloc(D2D3_DEVICE_NAME,0);
        if (!d2d3_platform_device) {
                printk("failed to alloc d2d3_platform_device\n");
                return -ENOMEM;
        }

        if(platform_device_add(d2d3_platform_device)){
                platform_device_put(d2d3_platform_device);
                printk("failed to add d2d3_platform_device\n");
                return -ENODEV;
        }
        if (platform_driver_register(&d2d3_driver)) {
                printk("failed to register d2d3 module\n");

                platform_device_del(d2d3_platform_device);
                platform_device_put(d2d3_platform_device);
                return -ENODEV;
        }
#else
        /*allocate major device number*/
        ret = alloc_chrdev_region(&d2d3_devno, 0, D2D3_COUNT, D2D3_DEVICE_NAME);
        if (ret < 0) {
                printk("[d2d3..]%s can't register major for d2d3 device.\n",__func__);
                goto failed_alloc_cdev_region;
        }
        d2d3_clsp = class_create(THIS_MODULE, D2D3_DEVICE_NAME);
        if (IS_ERR(d2d3_clsp)){
                ret = PTR_ERR(d2d3_clsp);
                printk(KERN_ERR "[d2d3..] %s create class error.\n",__func__);
                goto failed_create_class;
        }
        if (platform_driver_register(&d2d3_driver)) {
                printk("[d2d3..]%s failed to register d2d3 driver.\n",__func__);
                goto failed_register_driver;
        }
        printk("[d2d3..]%s:d2d3_init ok.\n",__func__);
        return ret;
#endif
failed_register_driver:
        class_destroy(d2d3_clsp);
failed_create_class:
        unregister_chrdev_region(d2d3_devno, D2D3_COUNT);
failed_alloc_cdev_region:
        return ret;
}




static void __exit d2d3_drv_exit(void)
{
        printk("[d2d3..]%s d2d3_exit.\n",__func__);
        class_destroy(d2d3_clsp);
        unregister_chrdev_region(d2d3_devno, D2D3_COUNT);
        platform_driver_unregister(&d2d3_driver);
        //platform_device_unregister(d2d3_platform_device);
        d2d3_platform_device = NULL;
        return ;
}


module_init(d2d3_drv_init);
module_exit(d2d3_drv_exit);

MODULE_DESCRIPTION("AMLOGIC D2D3 driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("2013-7-23a");


