#ifndef _BACKPORTS_LINUX_STATIC_KEY_H
#define _BACKPORTS_LINUX_STATIC_KEY_H 1

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0) /* kernels >= 3.3 */
/*
 * XXX: NOTE!
 *
 * Some 3.3 kernels carry <linux/static.h> but some don't even though its
 * its including <linux/jump_label.h>. What makes it more confusing is that
 * later all this got shuffled. The safe thing to do then is to just assume
 * kernels 3.3..3.4 don't have it and include <linux/jump_label.h> instead,
 * and for newer kernels include <linux/static_key.h>.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
#include_next <linux/static_key.h>
#else
#include <linux/jump_label.h>
#endif

#else /* kernels < 3.3 */
/*
 * in between 2.6.37 - 3.5 there's a slew of changes that make
 * it hard to backport this properly. If you are interested in
 * trying you can use this as reference:
 *
 * http://drvbp1.linux-foundation.org/~mcgrof/examples/2014/04/01/backport-static-keys.patch
 *
 * And these notes:
 *
 *           < v2.6.37 - No tracing support
 * bf5438fc  - v2.6.37 - Jump label support added primarily for tracing but
 *                       tracing was broken, later kernels started sporting
 *                       functional tracing.
 * d430d3d7e - v3.0    - Static branch optimizations for jump labels
 * c5905afb  - v3.3    - Static keys split out, note on the below issue
 * c5905afb  - v3.5    - git describe --contains c5905afb claims but not true!
 * c4b2c0c5f - v3.13   - Adds static_key_initialized(), STATIC_KEY_CHECK_USE()
 *
 * Because all of this we skip 2.6.37 - 3.3 but and adding support for older
 * can be done by of carrying over the non-architecture optimized code.
 * Carrying new changes into this file is a burden though so if we really
 * find use for this we could just split the non optimized versions upstream
 * and copy that through an automatic process.
 */
#endif /* kernels < 3.3 */

#endif /* _BACKPORTS_LINUX_STATIC_KEY_H */
