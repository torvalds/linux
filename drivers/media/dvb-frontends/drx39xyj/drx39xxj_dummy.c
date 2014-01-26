/* Dummy function to satisfy drxj.c */

#include <linux/types.h>
#include "drx_driver.h"


int drxbsp_tuner_set_frequency(struct tuner_instance *tuner,
				      u32 mode,
				      s32 center_frequency)
{
	return 0;
}

int
drxbsp_tuner_get_frequency(struct tuner_instance *tuner,
			   u32 mode,
			  s32 *r_ffrequency,
			  s32 *i_ffrequency)
{
	return 0;
}
