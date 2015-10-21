#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#define LINUX_RX_SIZE	(96 * 1024)
#define LINUX_TX_SIZE	(64 * 1024)
#define WILC1000_FW_SIZE (4 * 1024)

#define MALLOC_WILC_BUFFER(name, size)	\
	exported_ ## name = kmalloc(size, GFP_KERNEL);	  \
	if (!exported_ ## name) {   \
		printk("fail to alloc: %s memory\n", exported_ ## name);  \
		return -ENOBUFS;	\
	}

#define FREE_WILC_BUFFER(name)	\
	kfree(exported_ ## name);

/*
 * Add necessary buffer pointers
 */
void *exported_g_tx_buf;
void *exported_g_rx_buf;
void *exported_g_fw_buf;

void *get_tx_buffer(void)
{
	return exported_g_tx_buf;
}
EXPORT_SYMBOL(get_tx_buffer);

void *get_rx_buffer(void)
{
	return exported_g_rx_buf;
}
EXPORT_SYMBOL(get_rx_buffer);

void *get_fw_buffer(void)
{
	return exported_g_fw_buf;
}
EXPORT_SYMBOL(get_fw_buffer);

static int __init wilc_module_init(void)
{
	printk("wilc_module_init\n");
	/*
	 * alloc necessary memory
	 */
	MALLOC_WILC_BUFFER(g_tx_buf, LINUX_TX_SIZE)
	MALLOC_WILC_BUFFER(g_rx_buf, LINUX_RX_SIZE)
	MALLOC_WILC_BUFFER(g_fw_buf, WILC1000_FW_SIZE)

	return 0;
}

static void __exit wilc_module_deinit(void)
{
	printk("wilc_module_deinit\n");
	FREE_WILC_BUFFER(g_tx_buf)
	FREE_WILC_BUFFER(g_rx_buf)
	FREE_WILC_BUFFER(g_fw_buf)
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Tony Cho");
MODULE_DESCRIPTION("WILC1xxx Memory Manager");
pure_initcall(wilc_module_init);
module_exit(wilc_module_deinit);
