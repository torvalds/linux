.. SPDX-License-Identifier: GPL-2.0

==============================================
Netdev private dataroom for 6lowpan interfaces
==============================================

All 6lowpan able net devices, means all interfaces with ARPHRD_6LOWPAN,
must have "struct lowpan_priv" placed at beginning of netdev_priv.

The priv_size of each interface should be calculate by::

 dev->priv_size = LOWPAN_PRIV_SIZE(LL_6LOWPAN_PRIV_DATA);

Where LL_PRIV_6LOWPAN_DATA is sizeof linklayer 6lowpan private data struct.
To access the LL_PRIV_6LOWPAN_DATA structure you can cast::

 lowpan_priv(dev)-priv;

to your LL_6LOWPAN_PRIV_DATA structure.

Before registering the lowpan netdev interface you must run::

 lowpan_netdev_setup(dev, LOWPAN_LLTYPE_FOOBAR);

wheres LOWPAN_LLTYPE_FOOBAR is a define for your 6LoWPAN linklayer type of
enum lowpan_lltypes.

Example to evaluate the private usually you can do::

 static inline struct lowpan_priv_foobar *
 lowpan_foobar_priv(struct net_device *dev)
 {
	return (struct lowpan_priv_foobar *)lowpan_priv(dev)->priv;
 }

 switch (dev->type) {
 case ARPHRD_6LOWPAN:
	lowpan_priv = lowpan_priv(dev);
	/* do great stuff which is ARPHRD_6LOWPAN related */
	switch (lowpan_priv->lltype) {
	case LOWPAN_LLTYPE_FOOBAR:
		/* do 802.15.4 6LoWPAN handling here */
		lowpan_foobar_priv(dev)->bar = foo;
		break;
	...
	}
	break;
 ...
 }

In case of generic 6lowpan branch ("net/6lowpan") you can remove the check
on ARPHRD_6LOWPAN, because you can be sure that these function are called
by ARPHRD_6LOWPAN interfaces.
