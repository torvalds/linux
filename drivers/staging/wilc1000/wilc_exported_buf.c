#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#define LINUX_RX_SIZE	(96 * 1024)
#define LINUX_TX_SIZE	(64 * 1024)
#define WILC1000_FW_SIZE (4 * 1024)

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
	exported_g_tx_buf = kmalloc(LINUX_TX_SIZE, GFP_KERNEL);
	if (!exported_g_tx_buf)
		return -ENOMEM;

	exported_g_rx_buf = kmalloc(LINUX_RX_SIZE, GFP_KERNEL);
	if (!exported_g_rx_buf)
		goto free_g_tx_buf;

	exported_g_fw_buf = kmalloc(WILC1000_FW_SIZE, GFP_KERNEL);
	if (!exported_g_fw_buf)
		goto free_g_rx_buf;

	return 0;

free_g_rx_buf:
	kfree(exported_g_rx_buf);
	exported_g_rx_buf = NULL;

free_g_tx_buf:
	kfree(exported_g_tx_buf);
	exported_g_tx_buf = NULL;

	return -ENOMEM;
}

static void __exit wilc_module_deinit(void)
{
	printk("wilc_module_deinit\n");
	kfree(exported_g_tx_buf);
	kfree(exported_g_rx_buf);
	kfree(exported_g_fw_buf);
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Tony Cho");
MODULE_DESCRIPTION("WILC1xxx Memory Manager");
pure_initcall(wilc_module_init);
module_exit(wilc_module_deinit);
