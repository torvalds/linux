#include "nitrox_common.h"

int nitrox_crypto_register(void)
{
	int err;

	err = nitrox_register_skciphers();
	if (err)
		return err;

	err = nitrox_register_aeads();
	if (err) {
		nitrox_unregister_skciphers();
		return err;
	}

	return 0;
}

void nitrox_crypto_unregister(void)
{
	nitrox_unregister_aeads();
	nitrox_unregister_skciphers();
}
