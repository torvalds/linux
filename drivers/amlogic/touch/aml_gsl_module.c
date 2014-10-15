#include "linux/amlogic/input/aml_gsl_common.h"
extern unsigned int gsl_mask_tiaoping(void);
extern unsigned int gsl_version_id(void);
extern void gsl_alg_id_main(struct gsl_touch_info *cinfo);
extern void gsl_DataInit(int *ret);

static int __init aml_gsl_init(void)
{
	struct aml_gsl_api *api = kzalloc(sizeof (struct aml_gsl_api), GFP_KERNEL);
	printk("==%s==\n", __func__);
	if (!api)
		return -ENOMEM;
	api->gsl_mask_tiaoping = gsl_mask_tiaoping;
	api->gsl_version_id = gsl_version_id;
	api->gsl_alg_id_main = gsl_alg_id_main;
	api->gsl_DataInit = gsl_DataInit;
	aml_gsl_register_api(api);
	return 0;
}
static void __exit aml_gsl_exit(void)
{	
	struct aml_gsl_api *api = aml_gsl_get_api();
	printk("==%s==\n", __func__);
	if (api) {
		kfree(api);
		aml_gsl_clear_api();
	}
}

module_init(aml_gsl_init);
module_exit(aml_gsl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Amlogic Touch gsl driver");