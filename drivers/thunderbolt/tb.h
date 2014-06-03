/*
 * Thunderbolt Cactus Ridge driver - bus logic (NHI independent)
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef TB_H_
#define TB_H_

#include "ctl.h"

/**
 * struct tb - main thunderbolt bus structure
 */
struct tb {
	struct mutex lock;	/*
				 * Big lock. Must be held when accessing cfg or
				 * any struct tb_switch / struct tb_port.
				 */
	struct tb_nhi *nhi;
	struct tb_ctl *ctl;
	struct workqueue_struct *wq; /* ordered workqueue for plug events */
	bool hotplug_active; /*
			      * tb_handle_hotplug will stop progressing plug
			      * events and exit if this is not set (it needs to
			      * acquire the lock one more time). Used to drain
			      * wq after cfg has been paused.
			      */

};

struct tb *thunderbolt_alloc_and_start(struct tb_nhi *nhi);
void thunderbolt_shutdown_and_free(struct tb *tb);

#endif
