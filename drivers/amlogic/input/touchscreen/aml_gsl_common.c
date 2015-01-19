#include "linux/amlogic/input/aml_gsl_common.h"
static struct aml_gsl_api *g_aml_gsl_api = NULL;
int aml_gsl_register_api(struct aml_gsl_api *api)
{
    if (!api || g_aml_gsl_api) {
        printk("%s, invalid input, api:%p\n", __func__, g_aml_gsl_api);
        return -EINVAL;
    }
    g_aml_gsl_api = api;
    return 0;
}
EXPORT_SYMBOL(aml_gsl_register_api);

void aml_gsl_clear_api(void)
{
    g_aml_gsl_api = NULL;
}
EXPORT_SYMBOL(aml_gsl_clear_api);

struct aml_gsl_api* aml_gsl_get_api(void)
{
	return g_aml_gsl_api;
}
EXPORT_SYMBOL(aml_gsl_get_api);