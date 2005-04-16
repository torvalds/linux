#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/stddef.h"
#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/if_arp.h"
#include "net_kern.h"
#include "net_user.h"
#include "kern.h"
#include "slip.h"

struct slip_init {
	char *gate_addr;
};

void slip_init(struct net_device *dev, void *data)
{
	struct uml_net_private *private;
	struct slip_data *spri;
	struct slip_init *init = data;

	private = dev->priv;
	spri = (struct slip_data *) private->user;
	*spri = ((struct slip_data)
		{ .name 	= { '\0' },
		  .addr		= NULL,
		  .gate_addr 	= init->gate_addr,
		  .slave  	= -1,
		  .ibuf  	= { '\0' },
		  .obuf  	= { '\0' },
		  .pos 		= 0,
		  .esc 		= 0,
		  .dev 		= dev });

	dev->init = NULL;
	dev->hard_header_len = 0;
	dev->addr_len = 4;
	dev->type = ARPHRD_ETHER;
	dev->tx_queue_len = 256;
	dev->flags = IFF_NOARP;
	printk("SLIP backend - SLIP IP = %s\n", spri->gate_addr);
}

static unsigned short slip_protocol(struct sk_buff *skbuff)
{
	return(htons(ETH_P_IP));
}

static int slip_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	return(slip_user_read(fd, (*skb)->mac.raw, (*skb)->dev->mtu, 
			      (struct slip_data *) &lp->user));
}

static int slip_write(int fd, struct sk_buff **skb,
		      struct uml_net_private *lp)
{
	return(slip_user_write(fd, (*skb)->data, (*skb)->len, 
			       (struct slip_data *) &lp->user));
}

struct net_kern_info slip_kern_info = {
	.init			= slip_init,
	.protocol		= slip_protocol,
	.read			= slip_read,
	.write			= slip_write,
};

static int slip_setup(char *str, char **mac_out, void *data)
{
	struct slip_init *init = data;

	*init = ((struct slip_init)
		{ .gate_addr 		= NULL });

	if(str[0] != '\0') 
		init->gate_addr = str;
	return(1);
}

static struct transport slip_transport = {
	.list 		= LIST_HEAD_INIT(slip_transport.list),
	.name 		= "slip",
	.setup  	= slip_setup,
	.user 		= &slip_user_info,
	.kern 		= &slip_kern_info,
	.private_size 	= sizeof(struct slip_data),
	.setup_size 	= sizeof(struct slip_init),
};

static int register_slip(void)
{
	register_transport(&slip_transport);
	return(1);
}

__initcall(register_slip);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
