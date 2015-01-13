/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "priv.h"

#if defined(__powerpc__)
struct priv {
	const void __iomem *data;
	int size;
};

static u32
of_read(void *data, u32 offset, u32 length, struct nouveau_bios *bios)
{
	struct priv *priv = data;
	if (offset + length <= priv->size) {
		memcpy_fromio(bios->data + offset, priv->data + offset, length);
		return length;
	}
	return 0;
}

static void *
of_init(struct nouveau_bios *bios, const char *name)
{
	struct pci_dev *pdev = nv_device(bios)->pdev;
	struct device_node *dn;
	struct priv *priv;
	if (!(dn = pci_device_to_OF_node(pdev)))
		return ERR_PTR(-ENODEV);
	if (!(priv = kzalloc(sizeof(*priv), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	if ((priv->data = of_get_property(dn, "NVDA,BMP", &priv->size)))
		return priv;
	kfree(priv);
	return ERR_PTR(-EINVAL);
}

const struct nvbios_source
nvbios_of = {
	.name = "OpenFirmware",
	.init = of_init,
	.fini = (void(*)(void *))kfree,
	.read = of_read,
	.rw = false,
};
#else
const struct nvbios_source
nvbios_of = {
};
#endif
