#include "linux/kernel.h"
#include "linux/stddef.h"
#include "linux/init.h"
#include "linux/netdevice.h"
#include "linux/if_arp.h"
#include "net_kern.h"
#include "net_user.h"
#include "kern.h"
#include "slirp.h"

struct slirp_init {
	struct arg_list_dummy_wrapper argw;  /* XXX should be simpler... */
};

void slirp_init(struct net_device *dev, void *data)
{
	struct uml_net_private *private;
	struct slirp_data *spri;
	struct slirp_init *init = data;
	int i;

	private = dev->priv;
	spri = (struct slirp_data *) private->user;

	spri->argw = init->argw;
	spri->pid = -1;
	spri->slave = -1;
	spri->dev = dev;

	slip_proto_init(&spri->slip);

	dev->init = NULL;
	dev->hard_header_len = 0;
	dev->header_cache_update = NULL;
	dev->hard_header_cache = NULL;
	dev->hard_header = NULL;
	dev->addr_len = 0;
	dev->type = ARPHRD_SLIP;
	dev->tx_queue_len = 256;
	dev->flags = IFF_NOARP;
	printk("SLIRP backend - command line:");
	for(i=0;spri->argw.argv[i]!=NULL;i++) {
		printk(" '%s'",spri->argw.argv[i]);
	}
	printk("\n");
}

static unsigned short slirp_protocol(struct sk_buff *skbuff)
{
	return(htons(ETH_P_IP));
}

static int slirp_read(int fd, struct sk_buff **skb, 
		       struct uml_net_private *lp)
{
	return(slirp_user_read(fd, skb_mac_header(*skb), (*skb)->dev->mtu,
			      (struct slirp_data *) &lp->user));
}

static int slirp_write(int fd, struct sk_buff **skb,
		      struct uml_net_private *lp)
{
	return(slirp_user_write(fd, (*skb)->data, (*skb)->len, 
			       (struct slirp_data *) &lp->user));
}

const struct net_kern_info slirp_kern_info = {
	.init			= slirp_init,
	.protocol		= slirp_protocol,
	.read			= slirp_read,
	.write			= slirp_write,
};

static int slirp_setup(char *str, char **mac_out, void *data)
{
	struct slirp_init *init = data;
	int i=0;

	*init = ((struct slirp_init)
		{ .argw = { { "slirp", NULL  } } });

	str = split_if_spec(str, mac_out, NULL);

	if(str == NULL) { /* no command line given after MAC addr */
		return(1);
	}

	do {
		if(i>=SLIRP_MAX_ARGS-1) {
			printk("slirp_setup: truncating slirp arguments\n");
			break;
		}
		init->argw.argv[i++] = str;
		while(*str && *str!=',') {
			if(*str=='_') *str=' ';
			str++;
		}
		if(*str!=',')
			break;
		*str++='\0';
	} while(1);
	init->argw.argv[i]=NULL;
	return(1);
}

static struct transport slirp_transport = {
	.list 		= LIST_HEAD_INIT(slirp_transport.list),
	.name 		= "slirp",
	.setup  	= slirp_setup,
	.user 		= &slirp_user_info,
	.kern 		= &slirp_kern_info,
	.private_size 	= sizeof(struct slirp_data),
	.setup_size 	= sizeof(struct slirp_init),
};

static int register_slirp(void)
{
	register_transport(&slirp_transport);
	return 0;
}

late_initcall(register_slirp);
