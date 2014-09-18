#ifndef _SPARC64_MDESC_H
#define _SPARC64_MDESC_H

#include <linux/types.h>
#include <linux/cpumask.h>
#include <asm/prom.h>

struct mdesc_handle;

/* Machine description operations are to be surrounded by grab and
 * release calls.  The mdesc_handle returned from the grab is
 * the first argument to all of the operational calls that work
 * on mdescs.
 */
struct mdesc_handle *mdesc_grab(void);
void mdesc_release(struct mdesc_handle *);

#define MDESC_NODE_NULL		(~(u64)0)

u64 mdesc_node_by_name(struct mdesc_handle *handle,
		       u64 from_node, const char *name);
#define mdesc_for_each_node_by_name(__hdl, __node, __name) \
	for (__node = mdesc_node_by_name(__hdl, MDESC_NODE_NULL, __name); \
	     (__node) != MDESC_NODE_NULL; \
	     __node = mdesc_node_by_name(__hdl, __node, __name))

/* Access to property values returned from mdesc_get_property() are
 * only valid inside of a mdesc_grab()/mdesc_release() sequence.
 * Once mdesc_release() is called, the memory backed up by these
 * pointers may reference freed up memory.
 *
 * Therefore callers must make copies of any property values
 * they need.
 *
 * These same rules apply to mdesc_node_name().
 */
const void *mdesc_get_property(struct mdesc_handle *handle,
			       u64 node, const char *name, int *lenp);
const char *mdesc_node_name(struct mdesc_handle *hp, u64 node);

/* MD arc iteration, the standard sequence is:
 *
 *	unsigned long arc;
 *	mdesc_for_each_arc(arc, handle, node, MDESC_ARC_TYPE_{FWD,BACK}) {
 *		unsigned long target = mdesc_arc_target(handle, arc);
 *		...
 *	}
 */

#define MDESC_ARC_TYPE_FWD	"fwd"
#define MDESC_ARC_TYPE_BACK	"back"

u64 mdesc_next_arc(struct mdesc_handle *handle, u64 from,
		   const char *arc_type);
#define mdesc_for_each_arc(__arc, __hdl, __node, __type) \
	for (__arc = mdesc_next_arc(__hdl, __node, __type); \
	     (__arc) != MDESC_NODE_NULL; \
	     __arc = mdesc_next_arc(__hdl, __arc, __type))

u64 mdesc_arc_target(struct mdesc_handle *hp, u64 arc);

void mdesc_update(void);

struct mdesc_notifier_client {
	void (*add)(struct mdesc_handle *handle, u64 node);
	void (*remove)(struct mdesc_handle *handle, u64 node);

	const char			*node_name;
	struct mdesc_notifier_client	*next;
};

void mdesc_register_notifier(struct mdesc_notifier_client *client);

void mdesc_fill_in_cpu_data(cpumask_t *mask);
void mdesc_populate_present_mask(cpumask_t *mask);
void mdesc_get_page_sizes(cpumask_t *mask, unsigned long *pgsz_mask);

void sun4v_mdesc_init(void);

#endif
