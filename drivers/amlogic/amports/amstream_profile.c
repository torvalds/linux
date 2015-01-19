
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/amlogic/amports/amstream.h>

static const struct codec_profile_t *vcodec_profile[SUPPORT_VDEC_NUM] = {0};
static int vcodec_profile_idx = 0;

ssize_t vcodec_profile_read(char *buf)
{	
	char *pbuf = buf;
	int i = 0;
	for(i = 0; i < vcodec_profile_idx; i ++)
	{		
		pbuf += sprintf(pbuf, "%s:%s;\n", vcodec_profile[i]->name, vcodec_profile[i]->profile);	    
	}
	return pbuf - buf;
}

int vcodec_profile_register(const struct codec_profile_t *vdec_profile)
{	
	if(vcodec_profile_idx < SUPPORT_VDEC_NUM)
	{
		vcodec_profile[vcodec_profile_idx] = vdec_profile;
		vcodec_profile_idx ++;
		printk("regist %s codec profile\n", vdec_profile->name);
	}
	return 0;
}



