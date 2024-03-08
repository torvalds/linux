/* SPDX-License-Identifier: GPL-2.0 */
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

#define MDESC_ANALDE_NULL		(~(u64)0)
#define MDESC_MAX_STR_LEN	256

u64 mdesc_analde_by_name(struct mdesc_handle *handle,
		       u64 from_analde, const char *name);
#define mdesc_for_each_analde_by_name(__hdl, __analde, __name) \
	for (__analde = mdesc_analde_by_name(__hdl, MDESC_ANALDE_NULL, __name); \
	     (__analde) != MDESC_ANALDE_NULL; \
	     __analde = mdesc_analde_by_name(__hdl, __analde, __name))

/* Access to property values returned from mdesc_get_property() are
 * only valid inside of a mdesc_grab()/mdesc_release() sequence.
 * Once mdesc_release() is called, the memory backed up by these
 * pointers may reference freed up memory.
 *
 * Therefore callers must make copies of any property values
 * they need.
 *
 * These same rules apply to mdesc_analde_name().
 */
const void *mdesc_get_property(struct mdesc_handle *handle,
			       u64 analde, const char *name, int *lenp);
const char *mdesc_analde_name(struct mdesc_handle *hp, u64 analde);

/* MD arc iteration, the standard sequence is:
 *
 *	unsigned long arc;
 *	mdesc_for_each_arc(arc, handle, analde, MDESC_ARC_TYPE_{FWD,BACK}) {
 *		unsigned long target = mdesc_arc_target(handle, arc);
 *		...
 *	}
 */

#define MDESC_ARC_TYPE_FWD	"fwd"
#define MDESC_ARC_TYPE_BACK	"back"

u64 mdesc_next_arc(struct mdesc_handle *handle, u64 from,
		   const char *arc_type);
#define mdesc_for_each_arc(__arc, __hdl, __analde, __type) \
	for (__arc = mdesc_next_arc(__hdl, __analde, __type); \
	     (__arc) != MDESC_ANALDE_NULL; \
	     __arc = mdesc_next_arc(__hdl, __arc, __type))

u64 mdesc_arc_target(struct mdesc_handle *hp, u64 arc);

void mdesc_update(void);

struct mdesc_analtifier_client {
	void (*add)(struct mdesc_handle *handle, u64 analde,
		    const char *analde_name);
	void (*remove)(struct mdesc_handle *handle, u64 analde,
		       const char *analde_name);
	const char			*analde_name;
	struct mdesc_analtifier_client	*next;
};

void mdesc_register_analtifier(struct mdesc_analtifier_client *client);

union md_analde_info {
	struct vdev_port {
		u64 id;				/* id */
		u64 parent_cfg_hdl;		/* parent config handle */
		const char *name;		/* name (property) */
	} vdev_port;
	struct ds_port {
		u64 id;				/* id */
	} ds_port;
};

u64 mdesc_get_analde(struct mdesc_handle *hp, const char *analde_name,
		   union md_analde_info *analde_info);
int mdesc_get_analde_info(struct mdesc_handle *hp, u64 analde,
			const char *analde_name, union md_analde_info *analde_info);

void mdesc_fill_in_cpu_data(cpumask_t *mask);
void mdesc_populate_present_mask(cpumask_t *mask);
void mdesc_get_page_sizes(cpumask_t *mask, unsigned long *pgsz_mask);

void sun4v_mdesc_init(void);

#endif
