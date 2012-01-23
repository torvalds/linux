
#include "drv_hdmi_i.h"

__s32 DRV_HDMI_MInit(void)
{
    if(Hdmi_init() == -1)
    {
        __inf("Hdmi_Init() fail!\n");
   	    return -1;
    }

    return 0;

}

__s32 DRV_HDMI_MExit(void)
{
    __s32 ret;

    ret = Hdmi_exit();   /*hdmi module exit*/

    return ret;
}

int hdmi_open(struct inode *inode, struct file *file)
{
	return 0;
}

int hdmi_release(struct inode *inode, struct file *file)
{
	return 0;
}


ssize_t hdmi_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

ssize_t hdmi_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    return -EINVAL;
}

int hdmi_mmap(struct file *file, struct vm_area_struct * vma)
{
	return 0;
}

long hdmi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	__u8         uid;
	unsigned long karg[3];
	unsigned long aux = 0;
	void __user *ubuffer = NULL;
	unsigned long buf_size = 0;
	void *kbuffer = NULL;
	__s32 ret = 0;

	uid = iminor(file->f_path.dentry->d_inode);

	if (copy_from_user((void*)karg,(void __user*)arg,3*sizeof(unsigned long)))
	{
		__err("copy_from_user fail\n");
		return -EFAULT;
	}

	aux = *(unsigned long*)karg;
	ubuffer = (void __user*)(*(unsigned long*)(karg+1));
	buf_size = *(unsigned long*)(karg+2);

	if(buf_size != 0)
	{
		kbuffer = kmalloc(buf_size,GFP_KERNEL);
		if (copy_from_user(kbuffer, ubuffer,buf_size))
		{
			__err("copy_from_user fail\n");
			kfree(kbuffer);
			return -EFAULT;
		}
	}

	__msg("hdmi_ioctl,cmd:%x,aux:%x,ubuffer:%x\n",cmd,(unsigned int)aux,(unsigned int)ubuffer);
	switch(cmd)
	{
	case HDMI_CMD_SET_VIDEO_MOD:
		ret =  Hdmi_set_display_mode(aux);
		break;

	case HDMI_CMD_SET_AUDIO_PARA:
		ret = Hdmi_Set_Audio_Para((hdmi_audio_t *)kbuffer);
		break;

	case HDMI_CMD_CLOSE:
		ret =  Hdmi_close();
		break;

	case HDMI_CMD_MOD_SUPPORT:
		ret = Hdmi_mode_support((__u8)aux);
		break;

	case HDMI_CMD_AUDIO_ENABLE:
		ret = Hdmi_Audio_Enable(0, (__u8)aux);
		break;

	case HDMI_CMD_GET_HPD_STATUS:
		ret = Hdmi_get_HPD_status();
		break;

	default:
		ret = -1;
		break;
	}

	if(kbuffer)
	{
		kfree(kbuffer);
	}
	return ret;
}
