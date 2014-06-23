#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/flow_keys.h>
#include "enic_res.h"
#include "enic_clsf.h"

/* enic_addfltr_5t - Add ipv4 5tuple filter
 *	@enic: enic struct of vnic
 *	@keys: flow_keys of ipv4 5tuple
 *	@rq: rq number to steer to
 *
 * This function returns filter_id(hardware_id) of the filter
 * added. In case of error it returns an negative number.
 */
int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq)
{
	int res;
	struct filter data;

	switch (keys->ip_proto) {
	case IPPROTO_TCP:
		data.u.ipv4.protocol = PROTO_TCP;
		break;
	case IPPROTO_UDP:
		data.u.ipv4.protocol = PROTO_UDP;
		break;
	default:
		return -EPROTONOSUPPORT;
	};
	data.type = FILTER_IPV4_5TUPLE;
	data.u.ipv4.src_addr = ntohl(keys->src);
	data.u.ipv4.dst_addr = ntohl(keys->dst);
	data.u.ipv4.src_port = ntohs(keys->port16[0]);
	data.u.ipv4.dst_port = ntohs(keys->port16[1]);
	data.u.ipv4.flags = FILTER_FIELDS_IPV4_5TUPLE;

	spin_lock_bh(&enic->devcmd_lock);
	res = vnic_dev_classifier(enic->vdev, CLSF_ADD, &rq, &data);
	spin_unlock_bh(&enic->devcmd_lock);
	res = (res == 0) ? rq : res;

	return res;
}

/* enic_delfltr - Delete clsf filter
 *	@enic: enic struct of vnic
 *	@filter_id: filter_is(hardware_id) of filter to be deleted
 *
 * This function returns zero in case of success, negative number incase of
 * error.
 */
int enic_delfltr(struct enic *enic, u16 filter_id)
{
	int ret;

	spin_lock_bh(&enic->devcmd_lock);
	ret = vnic_dev_classifier(enic->vdev, CLSF_DEL, &filter_id, NULL);
	spin_unlock_bh(&enic->devcmd_lock);

	return ret;
}
