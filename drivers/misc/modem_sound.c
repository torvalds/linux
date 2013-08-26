#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/types.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/wait.h>
#include "modem_sound.h"
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#define ENABLE             1
#define DISABLE            0

static struct modem_sound_data *modem_sound;
int (*set_codec_for_pcm_modem)(int cmd) = NULL; /* Set the codec used only for PCM modem */
void (*set_codec_spk)(int on) = NULL;
#if defined(CONFIG_SND_RK_SOC_RK2928) ||  defined(CONFIG_SND_RK29_SOC_RK610_PHONEPAD)
extern void call_set_spk(int on);
#endif
#ifdef CONFIG_SND_SOC_ES8323_PCM
extern int set_es8323(int cmd);
#endif

int modem_sound_spkctl(int status)
{
	if(modem_sound->spkctl_io == INVALID_GPIO)
		return 0;
	if(status == ENABLE)
		gpio_direction_output(modem_sound->spkctl_io,GPIO_HIGH);//modem_sound->spkctl_io? GPIO_HIGH:GPIO_LOW);
	else 
		gpio_direction_output(modem_sound->spkctl_io,GPIO_LOW);	//modem_sound->spkctl_io? GPIO_LOW:GPIO_HIGH);
			
	return 0;
}

static void modem_sound_delay_power_downup(struct work_struct *work)
{
	struct modem_sound_data *pdata = container_of(work, struct modem_sound_data, work);
	if (pdata == NULL) {
		printk("%s: pdata = NULL\n", __func__);
		return;
	}

	down(&pdata->power_sem);
	up(&pdata->power_sem);
}

static int modem_sound_open(struct inode *inode, struct file *filp)
{
    DBG("modem_sound_open\n");

	return 0;
}

static ssize_t modem_sound_read(struct file *filp, char __user *ptr, size_t size, loff_t *pos)
{
	if (ptr == NULL)
		printk("%s: user space address is NULL\n", __func__);
	return sizeof(int);
}

static long modem_sound_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct modem_sound_data *pdata = modem_sound;

	DBG("modem_sound_ioctl: cmd = %d arg = %ld\n",cmd, arg);

	ret = down_interruptible(&pdata->power_sem);
	if (ret < 0) {
		printk("%s: down power_sem error ret = %ld\n", __func__, ret);
		return ret;
	}

	switch (cmd){
		case IOCTL_MODEM_EAR_PHOEN:
			DBG("modem_sound_ioctl: MODEM_EAR_PHONE\n");
			if (set_codec_for_pcm_modem)
				set_codec_for_pcm_modem(RCV);
			break;
		case IOCTL_MODEM_SPK_PHONE:
			DBG("modem_sound_ioctl: MODEM_SPK_PHONE\n");
			if (set_codec_for_pcm_modem)
				set_codec_for_pcm_modem(SPK_PATH);
			if(set_codec_spk)
				set_codec_spk(1);
			modem_sound_spkctl(ENABLE);
			break;
	  	case IOCTL_MODEM_HP_WITHMIC_PHONE:
	  		DBG("modem_sound_ioctl: MODEM_HP_WITHMIC_PHONE\n");
			if (set_codec_for_pcm_modem)
				set_codec_for_pcm_modem(HP_PATH);
			if(set_codec_spk)
				set_codec_spk(2);
			modem_sound_spkctl(DISABLE);
			break;
		case IOCTL_MODEM_BT_PHONE:
			if (set_codec_for_pcm_modem)
				set_codec_for_pcm_modem(BT);
			DBG("modem_sound_ioctl: MODEM_BT_PHONE\n");
			break;
		case IOCTL_MODEM_STOP_PHONE:
		  	DBG("modem_sound_ioctl: MODEM_STOP_PHONE\n");
			if(set_codec_spk)
				set_codec_spk(0);
			if (set_codec_for_pcm_modem)
				set_codec_for_pcm_modem(OFF);
			break;
	        case IOCTL_MODEM_HP_NOMIC_PHONE:
			DBG("modem_sound_ioctl: MODEM_HP_NOMIC_PHONE\n");
			if (set_codec_for_pcm_modem)
				set_codec_for_pcm_modem(HP_NO_MIC);
			if(set_codec_spk)
				set_codec_spk(2);
			modem_sound_spkctl(DISABLE);
			break;


		default:
			printk("unknown ioctl cmd!\n");
			ret = -EINVAL;
			break;
	}

	up(&pdata->power_sem);

	return ret;
}

static int modem_sound_release(struct inode *inode, struct file *filp)
{
    DBG("modem_sound_release\n");
    
	return 0;
}

static struct file_operations modem_sound_fops = {
	.owner   = THIS_MODULE,
	.open    = modem_sound_open,
	.read    = modem_sound_read,
	.unlocked_ioctl   = modem_sound_ioctl,
	.release = modem_sound_release,
};

static struct miscdevice modem_sound_dev = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "modem_sound",
    .fops = &modem_sound_fops,
};

static int modem_sound_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct modem_sound_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;

	ret = misc_register(&modem_sound_dev);
	if (ret < 0){
		printk("modem register err!\n");
		return ret;
	}
	
	sema_init(&pdata->power_sem,1);
	pdata->wq = create_freezable_workqueue("modem_sound");
	INIT_WORK(&pdata->work, modem_sound_delay_power_downup);
	modem_sound = pdata;
#if defined(CONFIG_SND_RK_SOC_RK2928)|| defined(CONFIG_SND_RK29_SOC_RK610_PHONEPAD)
        set_codec_spk = call_set_spk;
#endif
#ifdef CONFIG_SND_SOC_ES8323_PCM
	set_codec_for_pcm_modem = set_es8323;
#endif

	printk("%s:modem sound initialized\n",__FUNCTION__);

	return ret;
}

static int modem_sound_suspend(struct platform_device *pdev,  pm_message_t state)
{
	struct modem_sound_data *pdata = pdev->dev.platform_data;

	if(!pdata) {
		printk("%s: pdata = NULL ...... \n", __func__);
		return -1;
	}
	printk("%s\n",__FUNCTION__);
	return 0;	
}

static int modem_sound_resume(struct platform_device *pdev)
{
	struct modem_sound_data *pdata = pdev->dev.platform_data;

	if(!pdata) {
		printk("%s: pdata = NULL ...... \n", __func__);
		return -1;
	}
	printk("%s\n",__FUNCTION__);
	return 0;
}

static int modem_sound_remove(struct platform_device *pdev)
{
	struct modem_sound_data *pdata = pdev->dev.platform_data;
	if(!pdata)
		return -1;

	misc_deregister(&modem_sound_dev);

	return 0;
}

static struct platform_driver modem_sound_driver = {
	.probe	= modem_sound_probe,
	.remove = modem_sound_remove,
	.suspend  	= modem_sound_suspend,
	.resume		= modem_sound_resume,
	.driver	= {
		.name	= "modem_sound",
		.owner	= THIS_MODULE,
	},
};

static int __init modem_sound_init(void)
{
	return platform_driver_register(&modem_sound_driver);
}

static void __exit modem_sound_exit(void)
{
	platform_driver_unregister(&modem_sound_driver);
}

module_init(modem_sound_init);
module_exit(modem_sound_exit);
MODULE_DESCRIPTION ("modem sound driver");
MODULE_LICENSE("GPL");

