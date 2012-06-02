#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/wakelock.h>

#if 0
#define sc8800_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
//#define SC8800_PRINT_BUF
#else
#define sc8800_dbg(dev, format, arg...)
#endif

#define READ_TIMEOUT		30000  //no use
#define WRITE_TIMEOUT		80  //80ms

#define RD_BUF_SIZE			(16*PAGE_SIZE) // 64K
#define WR_BUF_SIZE			(2*PAGE_SIZE) // __get_free_pages(GFP_KERNEL, 1) ==> pages = 2^1 = 2
#define MAX_RX_LIST			256

#define BP_PACKET_SIZE		64
#define BP_PACKET_HEAD_LEN  16
#define BP_PACKET_DATA_LEN  48

struct plat_sc8800 {
	int slav_rts_pin;
	int slav_rdy_pin;
	int master_rts_pin;
	int master_rdy_pin;
	int poll_time;
	int (*io_init)(void);
	int (*io_deinit)(void);
};
struct bp_head{
	u16 tag;        //0x7e7f
	u16 type;       //0xaa55
	u32 length;     //the length of data after head(8192-128 bytes) 
	u32 fram_num;   //no used , always 0
	u32 reserved;   ////reserved 
	char data[BP_PACKET_DATA_LEN];
};

struct sc8800_data {
	struct device			*dev;
	struct spi_device		*spi;
	struct workqueue_struct *rx_wq;
	struct workqueue_struct *tx_wq;
	struct work_struct		rx_work;
	struct work_struct		tx_work;
	struct wake_lock 	rx_wake;
	struct wake_lock  tx_wake;

	wait_queue_head_t		waitrq;
	wait_queue_head_t		waitwq;
	spinlock_t				lock;

	int irq;
	int slav_rts;
	int slav_rdy;
	int master_rts;
	int master_rdy;

	int write_finished;
	int write_tmo;

	char *rx_buf;
	int	rx_len; 

	char *tx_buf;
	int	tx_len; 

	int rw_enable;

	int is_suspend;
};
static DEFINE_MUTEX(sc8800s_lock);
static DECLARE_RWSEM(sc8800_rsem);  
static DECLARE_RWSEM(sc8800_wsem);  
struct sc8800_data *g_sc8800 = NULL;

void *tmp_buf = NULL;
static int bp_rts(struct sc8800_data *sc8800)
{
	return gpio_get_value(sc8800->slav_rts);
}

static int bp_rdy(struct sc8800_data *sc8800)
{
	return gpio_get_value(sc8800->slav_rdy);
}

static void ap_rts(struct sc8800_data *sc8800, int value)
{
	gpio_set_value(sc8800->master_rts, value);
}

static void ap_rdy(struct sc8800_data *sc8800, int value)
{
	gpio_set_value(sc8800->master_rdy, value);
}
static void sc8800_print_buf(struct sc8800_data *sc8800, char *buf, const char *func, int len)
{
#ifdef SC8800_PRINT_BUF
	int i;
	char *tmp = NULL;
	tmp = kzalloc(len*7, GFP_KERNEL);
	sc8800_dbg(sc8800->dev, "%s buf[%d] = :\n", func, len);
	for(i = 0; i < len; i++){
		if(i % 16 == 0 && i != 0)
			sprintf(tmp, "%s\n", tmp);
		sprintf(tmp, "%s[0x%2x] ", tmp, buf[i]);
	}
	printk("%s\n", tmp);
	kfree(tmp);
#endif
	return;
}
static void buf_swp(char *buf, int len)
{
	int i;
	char temp;
		
	len = (len/2)*2;
	for(i = 0; i < len; i += 2)
	{
		temp = buf[i];
		buf[i] = buf[i+1];
		buf[i+1] = temp;
	}
}
static void spi_in(struct sc8800_data *sc8800, char *tx_buf, unsigned len, int* err)
{
	struct spi_message message;
	struct spi_transfer tran;

	tmp_buf = kzalloc(len, GFP_KERNEL);
	if(!tmp_buf){
		*err = -ENOMEM;
		return;
	}

	buf_swp(tx_buf, len);

	tran.tx_buf = (void *)tx_buf;
	tran.rx_buf = tmp_buf;
	tran.len = len;
	tran.speed_hz = 0;
	tran.bits_per_word = 16;

	spi_message_init(&message);
	spi_message_add_tail(&tran, &message);
	*err = spi_sync(sc8800->spi, &message);
	sc8800_print_buf(sc8800, tx_buf, __func__, len);
	kfree(tmp_buf);
}

static void spi_out(struct sc8800_data *sc8800, char *rx_buf, unsigned len, int* err)
{
	struct spi_message message;
	struct spi_transfer tran;

	tmp_buf = kzalloc(len, GFP_KERNEL);
	if(!tmp_buf){
		*err = -ENOMEM;
		return;
	}

	memset(rx_buf, 0, len);
	tran.tx_buf = tmp_buf;
	tran.rx_buf = (void *)rx_buf;
	tran.len = len;
	tran.speed_hz = 0;
	tran.bits_per_word = 16;

	spi_message_init(&message);
	spi_message_add_tail(&tran, &message);
	*err = spi_sync(sc8800->spi, &message);
	sc8800_print_buf(sc8800, rx_buf, __func__, len);

	buf_swp(rx_buf, len);
	kfree(tmp_buf);
}

static int ap_get_head(struct sc8800_data *sc8800, struct bp_head *packet)
{
	int err = 0, count = 5;
	char buf[BP_PACKET_SIZE];

retry:
	spi_out(sc8800, buf, BP_PACKET_SIZE, &err);

	if(err < 0 && count > 0)
	{
		dev_warn(sc8800->dev, "%s spi_out return error, retry count = %d\n",
				__func__, count);
		count--;
		mdelay(10);
		goto retry;
	}
	if(err < 0)
		return err;

	memcpy((char *)(packet), buf, BP_PACKET_SIZE);

	sc8800_dbg(sc8800->dev, "%s tag = 0x%4x, type = 0x%4x, length = %x\n",
			__func__, packet->tag, packet->type, packet->length);
		
	if ((packet->tag != 0x7e7f) || (packet->type != 0xaa55)) 
		return -1;
	else
		return 0;
}


static int sc8800_rx(struct sc8800_data *sc8800)
{
	int ret = 0, len, real_len;
	struct bp_head packet;
	char *buf = NULL;

	ap_rdy(sc8800,0);
	ret = ap_get_head(sc8800, &packet);

	if(ret < 0){
		dev_err(sc8800->dev, "ERR: %s ap_get_head err = %d\n", __func__, ret);
		goto out;
	}
	len = packet.length;
	if(len > BP_PACKET_DATA_LEN)
		real_len =	(((len -BP_PACKET_DATA_LEN-1)/BP_PACKET_SIZE)+2)*BP_PACKET_SIZE;
	else
		real_len = BP_PACKET_SIZE;
	if(len > RD_BUF_SIZE){
		dev_err(sc8800->dev, "ERR: %s len[%d] is large than buffer size[%lu]\n",
				__func__, real_len, RD_BUF_SIZE);	
		goto out;
	}
	buf = kzalloc(real_len, GFP_KERNEL);
	if(!buf){
		dev_err(sc8800->dev,"ERR: %s no memmory for rx_buf\n", __func__);
		goto out;
	}

	memcpy(buf, packet.data, BP_PACKET_DATA_LEN);
	if(len > BP_PACKET_DATA_LEN)
		spi_out(sc8800, buf + BP_PACKET_DATA_LEN, real_len-BP_PACKET_SIZE, &ret);
	
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: %s spi out err = %d\n", __func__, ret);
		goto out;
	}

	spin_lock(&sc8800->lock);
	if(sc8800->rx_len + len > RD_BUF_SIZE){
		dev_warn(sc8800->dev, "WARN: %s read buffer is full\n", __func__);
	}else
	{
		memcpy(sc8800->rx_buf+sc8800->rx_len, buf, len);
		sc8800->rx_len += len;
	}
	spin_unlock(&sc8800->lock);

	sc8800_dbg(sc8800->dev, "%s rx_len = %d\n", __func__, sc8800->rx_len);

	ret = sc8800->rx_len;

out:
	ap_rdy(sc8800,1);
	if(buf)
		kfree(buf);
	buf = NULL;
	return ret;

}
 static int sc8800_data_packet(char *dst, char *src, int src_len)
{
	int dst_len = 0;
	struct bp_head packet;

	if(src_len > BP_PACKET_DATA_LEN)
		dst_len = (((src_len -BP_PACKET_DATA_LEN-1)/BP_PACKET_SIZE)+2)*BP_PACKET_SIZE;
	else
		dst_len = BP_PACKET_SIZE;
	
	packet.tag =0x7e7f;
	packet.type = 0xaa55;
	packet.length = src_len;
	packet.fram_num = 0;
	packet.reserved = 0;
	
	memcpy(packet.data, src, BP_PACKET_DATA_LEN);	
	memcpy(dst, (char *)&packet, BP_PACKET_SIZE);
	if(src_len >= BP_PACKET_DATA_LEN)
		memcpy(dst+BP_PACKET_SIZE, src+BP_PACKET_DATA_LEN, src_len - BP_PACKET_DATA_LEN);

	return dst_len;
 }

static int sc8800_tx(struct sc8800_data *sc8800)
{
	int ret = 0, len;

	char *buf = NULL;

	sc8800->write_tmo = 0;
	buf = kzalloc(WR_BUF_SIZE + BP_PACKET_SIZE, GFP_KERNEL);
	if(!buf){
		dev_err(sc8800->dev, "ERR: no memery for buf\n");
		return -ENOMEM;
	}
	while(!bp_rts(sc8800)){
		if(sc8800->write_tmo){
			sc8800_dbg(sc8800->dev, "bp_rts = 0\n");
			kfree(buf);
			return -1;
		}
		schedule();
	}
	mutex_lock(&sc8800s_lock);
	#if defined(CONFIG_ARCH_RK30)
	ap_rts(sc8800,1);
	#else
	ap_rts(sc8800,0);
	#endif
#if 1
	while(bp_rdy(sc8800)){
		if(sc8800->write_tmo){
			#if defined(CONFIG_ARCH_RK30)
			ap_rts(sc8800,0);
			#else
			ap_rts(sc8800,1);
			#endif
			sc8800_dbg(sc8800->dev, "ERR: %s write timeout ->bp not ready (bp_rdy = 1)\n", __func__);
			msleep(1);
			kfree(buf);
			mutex_unlock(&sc8800s_lock);
			return -1;
		}
		schedule();
	}
#endif
	len = sc8800_data_packet(buf, sc8800->tx_buf, sc8800->tx_len);
	spi_in(sc8800, buf, len, &ret);

	if(ret < 0)
		dev_err(sc8800->dev, "ERR: %s spi in err = %d\n", __func__, ret);
	#if defined(CONFIG_ARCH_RK30)
	ap_rts(sc8800,0);
	#else
	ap_rts(sc8800,1);
	#endif
	if(buf){
		kfree(buf);
		buf = NULL;
	}

	mutex_unlock(&sc8800s_lock);
#if 1	
	while(!bp_rdy(sc8800)){
		if(sc8800->write_tmo){
			dev_err(sc8800->dev, "ERR: %s write timeout -> bp receiving (bp_rdy = 0)\n", __func__);
			ret = -1;
		}
		schedule();
	}
#endif
	return ret;
}

static irqreturn_t sc8800_irq(int irq, void *dev_id)
{
	struct sc8800_data *sc8800 = (struct sc8800_data *)dev_id;
	
	sc8800_dbg(sc8800->dev, "%s\n", __func__);
#if 0
	if(sc8800->is_suspend)
		rk28_send_wakeup_key();
#endif
	wake_lock(&sc8800->rx_wake);
	queue_work(sc8800->rx_wq, &sc8800->rx_work);
	return IRQ_HANDLED;
}

static void sc8800_rx_work(struct work_struct *rx_work)
{
	struct sc8800_data *sc8800 = container_of(rx_work, struct sc8800_data, rx_work);
	
	sc8800_dbg(sc8800->dev, "%s\n", __func__);
	mutex_lock(&sc8800s_lock);
	sc8800->rx_len = sc8800_rx(sc8800);
	wake_unlock(&sc8800->rx_wake);
	if(sc8800->rx_len <= 0)
		sc8800->rx_len = 0;
	wake_up(&sc8800->waitrq);
	mutex_unlock(&sc8800s_lock);
}
static void sc8800_tx_work(struct work_struct *tx_work)
{
	struct sc8800_data *sc8800 = container_of(tx_work, struct sc8800_data, tx_work);

	sc8800_dbg(sc8800->dev, "%s bp_rts = %d\n", __func__, bp_rts(sc8800));
	wake_lock(&sc8800->tx_wake);
	if(sc8800_tx(sc8800) == 0){
		sc8800->write_finished = 1;
		wake_up(&sc8800->waitwq);
	}
	wake_unlock(&sc8800->tx_wake);
}
static ssize_t sc8800_read(struct file *file,
			char __user *buf, size_t count, loff_t *offset)
{
	int ret = 0;
	ssize_t size = 0;
	struct sc8800_data *sc8800 = (struct sc8800_data *)file->private_data;

	sc8800_dbg(sc8800->dev, "%s count = %d\n", __func__, count);
	if(!buf){
		dev_err(sc8800->dev, "ERR: %s user_buf = NULL\n", __func__);
		return -EFAULT;
	}
	down_write(&sc8800_rsem);
	if(!(file->f_flags & O_NONBLOCK)){
		ret = wait_event_interruptible(sc8800->waitrq, (sc8800->rx_len > 0 || (sc8800->rw_enable <= 0)));
		if (ret) {
			up_write(&sc8800_rsem);
			return ret;
		}
	}
	if(sc8800->rw_enable <= 0){
		dev_err(sc8800->dev, "ERR: %s sc8800 is released\n", __func__);
		up_write(&sc8800_rsem);
		return -EFAULT;
	}
	if(sc8800->rx_len == 0){
		dev_warn(sc8800->dev, "WARN: %s nonblock read, rx_len = 0\n", __func__);
		return 0;
	}
	spin_lock(&sc8800->lock);
	sc8800_print_buf(sc8800, sc8800->rx_buf, __func__, sc8800->rx_len);
	if(sc8800->rx_len > count){
		size = count;
		ret = copy_to_user(buf, sc8800->rx_buf, count);
	}else{
		size = sc8800->rx_len;
		ret = copy_to_user(buf, sc8800->rx_buf, sc8800->rx_len);
	}

	if(ret < 0){
		dev_err(sc8800->dev, "ERR: %s copy to user ret = %d\n", __func__, ret);
		spin_unlock(&sc8800->lock);
		up_write(&sc8800_rsem);
		return -EFAULT;
	}
	if(sc8800->rx_len > count)
		memmove(sc8800->rx_buf, sc8800->rx_buf + count, sc8800->rx_len - count);
	
	sc8800->rx_len -= size;
	spin_unlock(&sc8800->lock);
	up_write(&sc8800_rsem);
	return size;
}
static ssize_t sc8800_write(struct file *file, 
			const char __user *buf, size_t count, loff_t *offset)
{
	int ret = 0;
	struct sc8800_data *sc8800 = (struct sc8800_data *)file->private_data;

	sc8800_dbg(sc8800->dev, "%s count = %d\n", __func__, count);
	if(count > WR_BUF_SIZE){
		dev_err(sc8800->dev, "ERR: %s count[%u] > WR_BUF_SIZE[%lu]\n",
				__func__, count, WR_BUF_SIZE);
		return -EFAULT;	
	}
	down_write(&sc8800_wsem);
	sc8800_print_buf(sc8800, sc8800->tx_buf, __func__, count);
	memset(sc8800->tx_buf, 0, WR_BUF_SIZE);
	ret = copy_from_user(sc8800->tx_buf, buf, count);
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: %s copy from user ret = %d\n", __func__, ret);
		up_write(&sc8800_wsem);
		return -EFAULT;
	}

	sc8800->write_finished = 0;
	sc8800->tx_len = count;
	queue_work(sc8800->tx_wq, &sc8800->tx_work);

	ret = wait_event_timeout(sc8800->waitwq,
				(sc8800->write_finished || sc8800->rw_enable <= 0),
				msecs_to_jiffies(WRITE_TIMEOUT));
	if(sc8800->rw_enable <= 0){
		dev_err(sc8800->dev, "ERR: %ssc8800 is released\n", __func__);
		up_write(&sc8800_wsem);
		return  -EFAULT;
	}
	if(ret == 0){
		sc8800->write_tmo = 1;
		dev_err(sc8800->dev, "ERR: %swrite timeout\n", __func__);
		up_write(&sc8800_wsem);
		return -ETIMEDOUT;
		//return count;
	}else{
		up_write(&sc8800_wsem);
		return count;
	}
}

static int sc8800_open(struct inode *inode, struct file *file)
{
	file->private_data = g_sc8800;
	
	sc8800_dbg(g_sc8800->dev, "%s\n", __func__);
	g_sc8800->write_finished = 0;
	g_sc8800->write_tmo = 0;
	g_sc8800->rw_enable++;
	return 0;
}
static int sc8800_release(struct inode *inode, struct file *file)
{
	struct sc8800_data *sc8800 = (struct sc8800_data *)file->private_data;

	sc8800_dbg(sc8800->dev, "%s\n", __func__);
	if(sc8800->rw_enable > 0)
		sc8800->rw_enable--;
	wake_up(&sc8800->waitrq);
	wake_up(&sc8800->waitwq);

	return 0;
}
static const struct file_operations sc8800_fops = {
	.open = sc8800_open,
	.release = sc8800_release,
	.read = sc8800_read,
	.write = sc8800_write,
};
static struct miscdevice sc8800_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sc8800",
	.fops = &sc8800_fops,
};

static int __devinit sc8800_probe(struct spi_device *spi)
{
	int ret = 0;
	unsigned long page;
	struct sc8800_data *sc8800 = NULL;
	struct plat_sc8800 *pdata = NULL;

	pdata = spi->dev.platform_data;
	if (!pdata) {
		dev_err(&spi->dev, "ERR: spi data missing\n");
		return -ENODEV;
	}

	sc8800 = (struct sc8800_data *)kzalloc(sizeof(struct sc8800_data), GFP_KERNEL);
	if(!sc8800){
		dev_err(&spi->dev, "ERR: no memory for sc8800\n");
		return -ENOMEM;
	}
	g_sc8800 = sc8800;

	page = __get_free_pages(GFP_KERNEL, 4); //2^4 * 4K = 64K
	if(!page){
		dev_err(&spi->dev, "ERR: no memory for rx_buf\n");
		ret = -ENOMEM;
		goto err_get_free_page1;
	}
	sc8800->rx_buf = (char *)page;

	page = __get_free_pages(GFP_KERNEL, 1);//2^1 * 4K = 8K
	if(!page){
		dev_err(&spi->dev, "ERR: no memory for tx_buf\n");
		ret = -ENOMEM;
		goto err_get_free_page2;
	}
	sc8800->tx_buf = (char *)page;

	sc8800->spi = spi;
	sc8800->dev = &spi->dev;
	dev_set_drvdata(sc8800->dev, sc8800);

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_2;
	spi->max_speed_hz = 1000*1000*8;
	ret = spi_setup(spi);
	if (ret < 0){
		dev_err(sc8800->dev, "ERR: fail to setup spi\n");
		goto err_spi_setup;
	}

	if(pdata && pdata->io_init)
		pdata->io_init();
	
	sc8800->irq = gpio_to_irq(pdata->slav_rts_pin);
	sc8800->slav_rts = pdata->slav_rts_pin;
	sc8800->slav_rdy = pdata->slav_rdy_pin;
	sc8800->master_rts = pdata->master_rts_pin;
	sc8800->master_rdy = pdata->master_rdy_pin;

	ret = gpio_request(sc8800->slav_rts, "salv_rts");
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: gpio request slav_rts[%d]\n", sc8800->slav_rts);
		goto err_gpio_request_slav_rts;
	}
	ret = gpio_request(sc8800->slav_rdy, "slav_rdy");
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: gpio request slav_rdy\n");
		goto err_gpio_request_slav_rdy;
	}
	ret = gpio_request(sc8800->master_rts, "master_rts");
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: gpio request master_rts\n");
		goto err_gpio_request_master_rts;
	}
	ret = gpio_request(sc8800->master_rdy, "master_rdy");
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: gpio request master_rdy\n");
		goto err_gpio_request_master_rdy;
	}
	gpio_direction_input(sc8800->slav_rts);
	gpio_pull_updown(sc8800->slav_rts, GPIOPullUp);
	gpio_direction_input(sc8800->slav_rdy);
	gpio_pull_updown(sc8800->slav_rdy, GPIOPullUp);
	gpio_direction_output(sc8800->master_rts, GPIO_HIGH);
	gpio_direction_output(sc8800->master_rdy, GPIO_HIGH);

	
	init_waitqueue_head(&sc8800->waitrq);
	init_waitqueue_head(&sc8800->waitwq);
	spin_lock_init(&sc8800->lock);

	ret = misc_register(&sc8800_device);
	if(ret < 0){
	
		dev_err(sc8800->dev, "ERR: fail to register misc device\n");
		goto err_misc_register;
	}
	ret = request_irq(sc8800->irq, sc8800_irq, IRQF_TRIGGER_FALLING, "sc8800", sc8800);
	if(ret < 0){
		dev_err(sc8800->dev, "ERR: fail to request irq %d\n", sc8800->irq);
		goto err_request_irq;
	}
	wake_lock_init(&sc8800->rx_wake, WAKE_LOCK_SUSPEND, "sc8800_rx_wake");
	wake_lock_init(&sc8800->tx_wake, WAKE_LOCK_SUSPEND, "sc8800_tx_wake");
	enable_irq_wake(sc8800->irq);
	sc8800->rx_wq = create_workqueue("sc8800_rxwq"); 
	sc8800->tx_wq = create_workqueue("sc8800_txwq"); 
	INIT_WORK(&sc8800->rx_work, sc8800_rx_work);
	INIT_WORK(&sc8800->tx_work, sc8800_tx_work);
	dev_info(sc8800->dev, "sc8800 probe ok\n");
	return 0;

err_request_irq:
	gpio_free(sc8800->master_rdy);
err_misc_register:
	misc_deregister(&sc8800_device);
err_gpio_request_master_rdy:
	gpio_free(sc8800->master_rts);
err_gpio_request_master_rts:
	gpio_free(sc8800->slav_rdy);
err_gpio_request_slav_rdy:
	gpio_free(sc8800->slav_rts);
err_gpio_request_slav_rts:
err_spi_setup:
	free_page((unsigned long)sc8800->tx_buf);
err_get_free_page2:
	free_page((unsigned long)sc8800->rx_buf);
err_get_free_page1:
	kfree(sc8800);
	sc8800 = NULL;
	g_sc8800 = NULL;
	return ret;
}

static int __devexit sc8800_remove(struct spi_device *spi)
{
	struct sc8800_data *sc8800 = dev_get_drvdata(&spi->dev);

	destroy_workqueue(sc8800->rx_wq); 
	destroy_workqueue(sc8800->tx_wq); 
	free_irq(sc8800->irq, sc8800);
	gpio_free(sc8800->master_rdy);
	gpio_free(sc8800->master_rts);
	gpio_free(sc8800->slav_rdy);
	gpio_free(sc8800->slav_rts);
	free_page((unsigned long)sc8800->tx_buf);
	free_page((unsigned long)sc8800->rx_buf);
	kfree(sc8800);
	sc8800 = NULL;
	return 0;
}

#ifdef CONFIG_PM

static int sc8800_suspend(struct spi_device *spi, pm_message_t state)
{
	struct sc8800_data *sc8800 = dev_get_drvdata(&spi->dev);

	sc8800->is_suspend = 1;	
	return 0;
}

static int sc8800_resume(struct spi_device *spi)
{
	struct sc8800_data *sc8800 = dev_get_drvdata(&spi->dev);


	sc8800->is_suspend = 0;	
	return 0;
}

#else
#define sc8800_suspend NULL
#define sc8800_resume  NULL
#endif

static struct spi_driver sc8800_driver = {
	.driver = {
		.name		= "sc8800",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= sc8800_probe,
	.remove		= __devexit_p(sc8800_remove),
	.suspend	= sc8800_suspend,
	.resume		= sc8800_resume,
};

static int __init sc8800_init(void)
{
	printk("sc8800_init\n");
	return spi_register_driver(&sc8800_driver);
}
module_init(sc8800_init);

static void __exit sc8800_exit(void)
{
	spi_unregister_driver(&sc8800_driver);
}
module_exit(sc8800_exit);

MODULE_DESCRIPTION("SC8800 driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:SC8800");
