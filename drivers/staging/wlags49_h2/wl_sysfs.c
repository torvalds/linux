/*
 * ex: sw=4
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/sysfs.h>

#include <debug.h>
#include <hcf.h>
#include <hcfdef.h>

#include <wl_if.h>
#include <wl_internal.h>
#include <wl_util.h>
#include <wl_main.h>
#include <wl_wext.h>
#include <wl_priv.h>

static inline int dev_isalive(const struct net_device *dev)
{
            return dev->reg_state == NETREG_REGISTERED;
}

/*
 * empirically even if tallies are defined as 32 bits entities, only
 * high 16 bits are relevant; low half is always zero. It means tallies
 * are pretty much useless for traffic counting but at least give overview
 * about where error come from
 */
static ssize_t show_tallies(struct device *d, struct device_attribute *attr,
			    char *buf)
{
    struct net_device *dev = to_net_dev(d);
    struct wl_private *lp = wl_priv(dev);
    unsigned long flags;
    CFG_HERMES_TALLIES_STRCT tallies;
    ssize_t ret = -EINVAL;

    rcu_read_lock();
    if (dev_isalive(dev)) {
	wl_lock(lp, &flags);

	ret = wl_get_tallies(lp, &tallies);
	if (ret == 0) {
		wl_unlock(lp, &flags);
		ret = snprintf(buf, PAGE_SIZE,
		    "TxUnicastFrames:           %u\n"
		    "TxMulticastFrames:         %u\n"
		    "TxFragments:               %u\n"
		    "TxUnicastOctets:           %u\n"
		    "TxMulticastOctets:         %u\n"
		    "TxDeferredTransmissions:   %u\n"
		    "TxSingleRetryFrames:       %u\n"
		    "TxMultipleRetryFrames:     %u\n"
		    "TxRetryLimitExceeded:      %u\n"
		    "TxDiscards:                %u\n"
		    "RxUnicastFrames:           %u\n"
		    "RxMulticastFrames:         %u\n"
		    "RxFragments:               %u\n"
		    "RxUnicastOctets:           %u\n"
		    "RxMulticastOctets:         %u\n"
		    "RxFCSErrors:               %u\n"
		    "RxDiscardsNoBuffer:        %u\n"
		    "TxDiscardsWrongSA:         %u\n"
		    "RxWEPUndecryptable:        %u\n"
		    "RxMsgInMsgFragments:       %u\n"
		    "RxMsgInBadMsgFragments:    %u\n"
		    "RxDiscardsWEPICVError:     %u\n"
		    "RxDiscardsWEPExcluded:     %u\n"
		    ,
		    (unsigned int)tallies.TxUnicastFrames,
		    (unsigned int)tallies.TxMulticastFrames,
		    (unsigned int)tallies.TxFragments,
		    (unsigned int)tallies.TxUnicastOctets,
		    (unsigned int)tallies.TxMulticastOctets,
		    (unsigned int)tallies.TxDeferredTransmissions,
		    (unsigned int)tallies.TxSingleRetryFrames,
		    (unsigned int)tallies.TxMultipleRetryFrames,
		    (unsigned int)tallies.TxRetryLimitExceeded,
		    (unsigned int)tallies.TxDiscards,
		    (unsigned int)tallies.RxUnicastFrames,
		    (unsigned int)tallies.RxMulticastFrames,
		    (unsigned int)tallies.RxFragments,
		    (unsigned int)tallies.RxUnicastOctets,
		    (unsigned int)tallies.RxMulticastOctets,
		    (unsigned int)tallies.RxFCSErrors,
		    (unsigned int)tallies.RxDiscardsNoBuffer,
		    (unsigned int)tallies.TxDiscardsWrongSA,
		    (unsigned int)tallies.RxWEPUndecryptable,
		    (unsigned int)tallies.RxMsgInMsgFragments,
		    (unsigned int)tallies.RxMsgInBadMsgFragments,
		    (unsigned int)tallies.RxDiscardsWEPICVError,
		    (unsigned int)tallies.RxDiscardsWEPExcluded);
	    } else {
		wl_unlock( lp, &flags );
	    }
    }

    rcu_read_unlock();
    return ret;
}

static DEVICE_ATTR(tallies, S_IRUGO, show_tallies, NULL);

static struct attribute *wlags_attrs[] = {
    &dev_attr_tallies.attr,
    NULL
};

static struct attribute_group wlags_group = {
    .name = "wlags",
    .attrs = wlags_attrs,
};

void register_wlags_sysfs(struct net_device *net)
{
    struct device *dev = &(net->dev);
    struct wl_private *lp = wl_priv(net);

    lp->sysfsCreated = sysfs_create_group(&dev->kobj, &wlags_group);
}

void unregister_wlags_sysfs(struct net_device *net)
{
    struct device *dev = &(net->dev);
    struct wl_private *lp = wl_priv(net);

    if (lp->sysfsCreated)
	sysfs_remove_group(&dev->kobj, &wlags_group);
}
