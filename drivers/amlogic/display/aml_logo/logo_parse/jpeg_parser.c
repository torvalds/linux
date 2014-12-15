/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *		parse bmp data 
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
#include "logo.h"
#include <mach/am_regs.h>
#include <asm/cacheflush.h>
#include	"amlogo_log.h"
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/amports/canvas.h>
#include "amvdec.h"
#include "vmjpeg_mc.h"
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include "jpeg_parser.h"
#include <linux/delay.h>
#include <linux/syscalls.h>


static  logo_parser_t    logo_jpeg_parser={
 	.name="jpg",
	.op={
	 .init = jpeg_init,
	 .decode=jpeg_decode,
	 .deinit=jpeg_deinit,
	}
 };
jpeg_private_t *g_jpeg_parser;


static vframe_t *jpeglogo_vf_peek(void *para)
{
	return (g_jpeg_parser->state== PIC_DECODED) ? &g_jpeg_parser->vf : NULL;
}

static vframe_t *jpeglogo_vf_get(void *para)
{
	if (g_jpeg_parser->state == PIC_DECODED) {
		g_jpeg_parser->state = PIC_FETCHED;
		return &g_jpeg_parser->vf;
	}
	
	return NULL;
}
static const struct vframe_operations_s jpeglogo_vf_provider_op =
{
    	.peek = jpeglogo_vf_peek,
    	.get  = jpeglogo_vf_get,
    	.put  = NULL,
};
#ifdef CONFIG_AM_VIDEO 
static struct vframe_provider_s jpeglogo_vf_prov;
#endif
static inline u32 index2canvas(u32 index)
{
    const u32 canvas_tab[4] = {
        0x020100, 0x050403, 0x080706, 0x0b0a09
    };

    return canvas_tab[index];
}

#if 0
static irqreturn_t jpeglogo_isr(int irq, void *dev_id)
{
	u32 reg, index;
     	vframe_t  *pvf=&g_jpeg_parser->vf;	

    	WRITE_MPEG_REG(VDEC_ASSIST_MBOX1_CLR_REG, 1);

    	reg = READ_MPEG_REG(MREG_FROM_AMRISC);

    	if (reg & PICINFO_BUF_IDX_MASK) {
       		index = ((reg & PICINFO_BUF_IDX_MASK) - 1) & 3;

        	pvf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
        	pvf->canvas0Addr = pvf->canvas1Addr = index2canvas(index);
		g_jpeg_parser->state= PIC_DECODED;
		g_jpeg_parser->canvas_index=pvf->canvas0Addr;
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"[jpeglogo]: One frame decoded\n");
		
    }

    return IRQ_HANDLED;
}
#endif

static s32 parse_jpeg_info(u8 *dp,logo_object_t *plogo)
{
	int len = 0;
	int end=0;
	u8 *p = dp + 2;
	u8 tag;
	int	ret=-EINVAL;


	if((u32)dp&7) goto exit ;  //logo not align at 8byte boundary.
	if ((dp[0] != JPEG_TAG) ||
		(dp[1] != JPEG_TAG_SOI))
		goto exit;

	if (*p++ != JPEG_TAG)
		goto exit;
		
	tag = *p++;
	len = ((u32)(p[0]) << 8) | p[1];
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"picture format jpeg\n");
	plogo->parser=&logo_jpeg_parser;
	while (!end) {
		switch(tag)
		{
			case  JPEG_TAG_SOF0: //get picture info
			plogo->parser->logo_pic_info.height= ((u32)p[3] << 8) | p[4];
			plogo->parser->logo_pic_info.width = ((u32)p[5] << 8) | p[6];
			plogo->parser->logo_pic_info.color_info=p[7]*8;
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"[picture info]%dx%d \n",plogo->parser->logo_pic_info.width ,plogo->parser->logo_pic_info.height);
			break;
			case JPEG_TAG_SOS: //goto file end
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"[0x%p]start scan line\n",p);
			while(p[0]!=JPEG_TAG || p[1]!=JPEG_TAG_EOI )
			{
				p++;						//to speed up we need setup file_size outof parser.
				if(p-dp>JPEG_INVALID_FILE_SIZE)//exception check
				{
					end=1;
					continue;
				}
			}
			ret=p-dp+2;
			end=1;
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"jpeg parser end,file size:%d\n",ret);
			continue;
			default:
			break;
		}
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"tag:0x%x,len:0x%x\n",tag,len);
		p += len;
		if (*p++ != JPEG_TAG)
		break;

		tag = *p++;
		len = ((u32)p[0] << 8) | p[1];
		
	}
exit:	
	return ret;
}
/* addr must be aligned at 8 bytes boundary */
static void swap_tailzero_data(u8 *addr, u32 s)
{
	register u8 d;
	u8 *p = addr;
	u32 len = (s + 7) >> 3;
	
	memset(addr + s, 0, PADDINGSIZE);

	while (len > 0) {
		d = p[0]; p[0] = p[7]; p[7] = d;
		d = p[1]; p[1] = p[6]; p[6] = d;
		d = p[2]; p[2] = p[5]; p[5] = d;
		d = p[3]; p[3] = p[4]; p[4] = d;
		p += 8;
		len --;
	}
}

static void jpeglogo_canvas_init(logo_object_t *plogo)
{
       int i;
       u32 canvas_width, canvas_height;
       u32 decbuf_size, decbuf_y_size, decbuf_uv_size;
	jpeg_private_t *priv=(jpeg_private_t *)plogo->parser->priv;
	u32 disp=plogo->platform_res[LOGO_DEV_VID].mem_start;//decode to video layer.
    
        if ((priv->vf.width< 768) && (priv->vf.height< 576)) {
            /* SD only */
            canvas_width   = 768;
            canvas_height  = 576;
            decbuf_y_size  = 0x80000;
            decbuf_uv_size = 0x20000;
            decbuf_size    = 0x100000;
        }
        else {
            /* HD & SD */
            canvas_width   = 1920;
            canvas_height  = 1088;
            decbuf_y_size  = 0x200000;
            decbuf_uv_size = 0x80000;
            decbuf_size    = 0x300000;
        }
    
        for (i = 0; i < 4; i++) {
            canvas_config(3 * i + 0,
                          disp + i * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(3 * i + 1,
                          disp + i * decbuf_size + decbuf_y_size,
                          canvas_width / 2, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(3 * i + 2,
                          disp + i * decbuf_size + decbuf_y_size + decbuf_uv_size,
                          canvas_width/2, canvas_height/2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
	    		
        }
}
static void init_scaler(void)
{
    /* 4 point triangle */
    const unsigned filt_coef[] = {
        0x20402000, 0x20402000, 0x1f3f2101, 0x1f3f2101,
        0x1e3e2202, 0x1e3e2202, 0x1d3d2303, 0x1d3d2303,
        0x1c3c2404, 0x1c3c2404, 0x1b3b2505, 0x1b3b2505,
        0x1a3a2606, 0x1a3a2606, 0x19392707, 0x19392707,
        0x18382808, 0x18382808, 0x17372909, 0x17372909,
        0x16362a0a, 0x16362a0a, 0x15352b0b, 0x15352b0b,
        0x14342c0c, 0x14342c0c, 0x13332d0d, 0x13332d0d,
        0x12322e0e, 0x12322e0e, 0x11312f0f, 0x11312f0f,
        0x10303010
    };
    int i;

    /* pscale enable, PSCALE cbus bmem enable */
    WRITE_MPEG_REG(PSCALE_CTRL, 0xc000);

    /* write filter coefs */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 0);
    for (i = 0; i < 33; i++) {
        WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0);
        WRITE_MPEG_REG(PSCALE_BMEM_DAT, filt_coef[i]);
    }

    /* Y horizontal initial info */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 37*2);
    /* [35]: buf repeat pix0,
     * [34:29] => buf receive num,
     * [28:16] => buf blk x,
     * [15:0] => buf phase
     */
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x0008);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x60000000);

    /* C horizontal initial info */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 41*2);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x0008);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x60000000);

    /* Y vertical initial info */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 39*2);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x0008);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x60000000);

    /* C vertical initial info */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 43*2);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x0008);
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x60000000);

    /* Y horizontal phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 36*2 + 1);
    /* [19:0] => Y horizontal phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x10000);
    /* C horizontal phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 40*2 + 1);
    /* [19:0] => C horizontal phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x10000);

    /* Y vertical phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 38*2+1);
    /* [19:0] => Y vertical phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x10000);
    /* C vertical phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_ADDR, 42*2+1);
    /* [19:0] => C horizontal phase step */
    WRITE_MPEG_REG(PSCALE_BMEM_DAT, 0x10000);

    /* reset pscaler */
    WRITE_MPEG_REG(RESET2_REGISTER, RESET_PSCALE);
	READ_MPEG_REG(RESET2_REGISTER);
	READ_MPEG_REG(RESET2_REGISTER);
	READ_MPEG_REG(RESET2_REGISTER);

    WRITE_MPEG_REG(PSCALE_RST, 0x7);
    WRITE_MPEG_REG(PSCALE_RST, 0x0);
}

static void jpeglogo_prot_init(logo_object_t *plogo)
{
	WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC);
   
       jpeglogo_canvas_init(plogo);
   
       WRITE_MPEG_REG(AV_SCRATCH_0, 12);
       WRITE_MPEG_REG(AV_SCRATCH_1, 0x031a);
       WRITE_MPEG_REG(AV_SCRATCH_4, 0x020100);
       WRITE_MPEG_REG(AV_SCRATCH_5, 0x050403);
       WRITE_MPEG_REG(AV_SCRATCH_6, 0x080706);
       WRITE_MPEG_REG(AV_SCRATCH_7, 0x0b0a09);
   
       init_scaler();
   
       /* clear buffer IN/OUT registers */
       WRITE_MPEG_REG(MREG_TO_AMRISC, 0);
       WRITE_MPEG_REG(MREG_FROM_AMRISC, 0);
   
       WRITE_MPEG_REG(MCPU_INTR_MSK, 0xffff);
       WRITE_MPEG_REG(MREG_DECODE_PARAM, 0);
   
       /* clear mailbox interrupt */
       WRITE_MPEG_REG(VDEC_ASSIST_MBOX1_CLR_REG, 1);
       /* enable mailbox interrupt */
       WRITE_MPEG_REG(VDEC_ASSIST_MBOX1_MASK, 1);
}
static void setup_vb(u32 addr, int s)
{
    WRITE_MPEG_REG(VLD_MEM_VIFIFO_START_PTR, addr);
    WRITE_MPEG_REG(VLD_MEM_VIFIFO_CURR_PTR, addr);
    WRITE_MPEG_REG(VLD_MEM_VIFIFO_END_PTR, (addr + s + PADDINGSIZE + 7) & ~7);

    SET_MPEG_REG_MASK(VLD_MEM_VIFIFO_CONTROL, MEM_BUFCTRL_INIT);
    CLEAR_MPEG_REG_MASK(VLD_MEM_VIFIFO_CONTROL, MEM_BUFCTRL_INIT);

    WRITE_MPEG_REG(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_MANUAL);
    WRITE_MPEG_REG(VLD_MEM_VIFIFO_WP, addr);

    SET_MPEG_REG_MASK(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);
    CLEAR_MPEG_REG_MASK(VLD_MEM_VIFIFO_BUF_CNTL, MEM_BUFCTRL_INIT);

    SET_MPEG_REG_MASK(VLD_MEM_VIFIFO_CONTROL, MEM_FILL_ON_LEVEL | MEM_CTRL_FILL_EN | MEM_CTRL_EMPTY_EN);
}
static inline void feed_vb(s32 s)
{
	u32 addr = READ_MPEG_REG(VLD_MEM_VIFIFO_START_PTR);
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"feed start addr:0x%x\n",addr);
    	WRITE_MPEG_REG(VLD_MEM_VIFIFO_WP, (addr + s + PADDINGSIZE + 7) & ~7);
}

static int hardware_init(logo_object_t *plogo,int logo_size)
{
#ifdef CONFIG_AM_STREAMING
	u32	*mc_addr_aligned = (u32 *)vmjpeg_mc;
#endif
	int ret = 0;
	if(plogo->para.output_dev_type  <=LOGO_DEV_VID ) //now only support display on video layer.
	{
		if(plogo->para.output_dev_type < LOGO_DEV_VID)
		plogo->need_transfer=TRUE;
		else
		plogo->need_transfer=FALSE;	
	}
	else
	{
		return -EINVAL;
	}
	WRITE_MPEG_REG(RESET0_REGISTER, RESET_VCPU | RESET_CCPU);
#ifdef CONFIG_AM_STREAMING	
	if (amvdec_loadmc(mc_addr_aligned) < 0) {
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"[jpeglogo]: Can not loading HW decoding ucode.\n");
        	return -EBUSY;
    	}
#endif	
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"load micro code completed\n");
	jpeglogo_prot_init(plogo);
	
	/*ret= request_irq(INT_MAILBOX_1A, jpeglogo_isr,
                    IRQF_SHARED, "jpeglogo-irq", (void *)hardware_init);*/
	
    	if (ret) {
        amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"jpeglogo_init irq register error.\n");
        return -ENOENT;
    	}
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"jpeg irq request ok\n");	
	setup_vb((u32)virt_to_phys(plogo->para.mem_addr),logo_size);
	WRITE_MPEG_REG(M4_CONTROL_REG, 0x0300);
	WRITE_MPEG_REG(POWER_CTL_VLD, 0);
	//set initial screen mode :
	
	
	return SUCCESS;


}

static int jpeg_init(logo_object_t *plogo)
{
	int  logo_size;
	void  __iomem* vaddr;
	jpeg_private_t  *priv;

	vaddr=(void  __iomem*)plogo->para.mem_addr;
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"logo vaddr:0x%p\n ",vaddr);
	if((logo_size=parse_jpeg_info(vaddr,plogo)) <=0 )
	return PARSER_UNFOUND;
	vaddr = ioremap_wc((unsigned int)virt_to_phys(plogo->para.mem_addr), logo_size + PADDINGSIZE);
	if(NULL==vaddr)
	{
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"remapping logo data failed\n");
		return  -ENOMEM;
	}
	priv=(jpeg_private_t  *)kmalloc(sizeof(jpeg_private_t),GFP_KERNEL);
	if(IS_ERR(priv))
	{
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"can't alloc memory for jpeg private data\n");
		return  -ENOMEM;
	}
	memset(priv, 0, sizeof(jpeg_private_t));
	priv->vf.width=plogo->parser->logo_pic_info.width;
	priv->vf.height=plogo->parser->logo_pic_info.height;
	plogo->parser->priv=priv;
	g_jpeg_parser=priv;
	priv->vaddr=vaddr;
	swap_tailzero_data((u8 *)vaddr, logo_size);
	if(hardware_init(plogo,logo_size) !=SUCCESS)
	{
		return PARSER_UNFOUND;
	}
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"jpeg parser hardware init ok\n");
	plogo->parser->logo_pic_info.size=logo_size;
	return PARSER_FOUND;
}
static  int  thread_progress(void *para)
{
	logo_object_t *plogo=(logo_object_t*)para;
	jpeg_private_t *priv=(jpeg_private_t*)plogo->parser->priv;
	ulong timeout;
		
	timeout = jiffies + HZ*8;
    	while (time_before(jiffies, timeout)) {
		if (priv->state== PIC_FETCHED)
		{
#ifdef CONFIG_AM_VIDEO 	
			vf_unreg_provider(&jpeglogo_vf_prov);
#endif
			kfree(priv);
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"logo fetched\n");
			return SUCCESS;
		}	
	}
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"logo unfetched\n");
	return FAIL;
}
static  int  jpeg_decode(logo_object_t *plogo)
{
	ulong timeout;
	jpeg_private_t *priv=(jpeg_private_t*)plogo->parser->priv;
#ifdef CONFIG_AM_STREAMING	
	amvdec_start();
#endif
       	feed_vb(plogo->parser->logo_pic_info.size);
	timeout = jiffies + HZ * 2;//wait 2s
    
    	while (time_before(jiffies, timeout)) {
		if (priv->state == PIC_DECODED) {
			/* disable OSD layer to expose logo on video layer  */
			amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"[jpeglogo]: logo decoded.\n");
			break;
		}
    	}
#ifdef CONFIG_AM_STREAMING		
    	amvdec_stop();
#endif
	/*free_irq(INT_MAILBOX_1A, (void *)hardware_init);*/
	if (priv->state > PIC_NA) 
	{
		if(plogo->para.output_dev_type == LOGO_DEV_VID)
		{
#ifdef CONFIG_AM_VIDEO 
                     vf_provider_init(&jpeglogo_vf_prov, "jpeglogo_provider", &jpeglogo_vf_provider_op, NULL);
			vf_reg_provider(&jpeglogo_vf_prov);
#endif
			kernel_thread(thread_progress, plogo, 0);
		}else
		{
			plogo->parser->decoder.jpg.out_canvas_index=priv->canvas_index;
			kfree(priv);
		}
    		
	}
	else
	{
		amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"deocod jpeg uncompleted\n");
		return FAIL;
	}
	return SUCCESS;
}
static int  jpeg_deinit(logo_object_t *plogo)
{
	jpeg_private_t	*priv=(jpeg_private_t*)plogo->parser->priv;

	if(priv)
	{
		if(priv->vaddr)
		{
			iounmap(priv->vaddr);
		}
	}
	return SUCCESS;
}
int jpeg_setup(void)
{
	register_logo_parser(&logo_jpeg_parser);
	amlog_mask_level(LOG_MASK_PARSER,LOG_LEVEL_LOW,"jpeg parser setup\n");
	return SUCCESS;
}
