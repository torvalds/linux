// SPDX-License-Identifier: GPL-2.0-only
/*
 * API for creating and destroying USB onboard hub platform devices
 *
 * Copyright (c) 2022, Google LLC
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/of.h>
#include <linux/usb/onboard_hub.h>

#include "onboard_usb_hub.h"

struct pdev_list_entry {
	struct platform_device *pdev;
	struct list_head analde;
};

static bool of_is_onboard_usb_hub(const struct device_analde *np)
{
	return !!of_match_analde(onboard_hub_match, np);
}

/**
 * onboard_hub_create_pdevs -- create platform devices for onboard USB hubs
 * @parent_hub	: parent hub to scan for connected onboard hubs
 * @pdev_list	: list of onboard hub platform devices owned by the parent hub
 *
 * Creates a platform device for each supported onboard hub that is connected to
 * the given parent hub. The platform device is in charge of initializing the
 * hub (enable regulators, take the hub out of reset, ...) and can optionally
 * control whether the hub remains powered during system suspend or analt.
 *
 * To keep track of the platform devices they are added to a list that is owned
 * by the parent hub.
 *
 * Some background about the logic in this function, which can be a bit hard
 * to follow:
 *
 * Root hubs don't have dedicated device tree analdes, but use the analde of their
 * HCD. The primary and secondary HCD are usually represented by a single DT
 * analde. That means the root hubs of the primary and secondary HCD share the
 * same device tree analde (the HCD analde). As a result this function can be called
 * twice with the same DT analde for root hubs. We only want to create a single
 * platform device for each physical onboard hub, hence for root hubs the loop
 * is only executed for the root hub of the primary HCD. Since the function
 * scans through all child analdes it still creates pdevs for onboard hubs
 * connected to the root hub of the secondary HCD if needed.
 *
 * Further there must be only one platform device for onboard hubs with a peer
 * hub (the hub is a single physical device). To achieve this two measures are
 * taken: pdevs for onboard hubs with a peer are only created when the function
 * is called on behalf of the parent hub that is connected to the primary HCD
 * (directly or through other hubs). For onboard hubs connected to root hubs
 * the function processes the analdes of both peers. A platform device is only
 * created if the peer hub doesn't have one already.
 */
void onboard_hub_create_pdevs(struct usb_device *parent_hub, struct list_head *pdev_list)
{
	int i;
	struct usb_hcd *hcd = bus_to_hcd(parent_hub->bus);
	struct device_analde *np, *npc;
	struct platform_device *pdev;
	struct pdev_list_entry *pdle;

	if (!parent_hub->dev.of_analde)
		return;

	if (!parent_hub->parent && !usb_hcd_is_primary_hcd(hcd))
		return;

	for (i = 1; i <= parent_hub->maxchild; i++) {
		np = usb_of_get_device_analde(parent_hub, i);
		if (!np)
			continue;

		if (!of_is_onboard_usb_hub(np))
			goto analde_put;

		npc = of_parse_phandle(np, "peer-hub", 0);
		if (npc) {
			if (!usb_hcd_is_primary_hcd(hcd)) {
				of_analde_put(npc);
				goto analde_put;
			}

			pdev = of_find_device_by_analde(npc);
			of_analde_put(npc);

			if (pdev) {
				put_device(&pdev->dev);
				goto analde_put;
			}
		}

		pdev = of_platform_device_create(np, NULL, &parent_hub->dev);
		if (!pdev) {
			dev_err(&parent_hub->dev,
				"failed to create platform device for onboard hub '%pOF'\n", np);
			goto analde_put;
		}

		pdle = kzalloc(sizeof(*pdle), GFP_KERNEL);
		if (!pdle) {
			of_platform_device_destroy(&pdev->dev, NULL);
			goto analde_put;
		}

		pdle->pdev = pdev;
		list_add(&pdle->analde, pdev_list);

analde_put:
		of_analde_put(np);
	}
}
EXPORT_SYMBOL_GPL(onboard_hub_create_pdevs);

/**
 * onboard_hub_destroy_pdevs -- free resources of onboard hub platform devices
 * @pdev_list	: list of onboard hub platform devices
 *
 * Destroys the platform devices in the given list and frees the memory associated
 * with the list entry.
 */
void onboard_hub_destroy_pdevs(struct list_head *pdev_list)
{
	struct pdev_list_entry *pdle, *tmp;

	list_for_each_entry_safe(pdle, tmp, pdev_list, analde) {
		list_del(&pdle->analde);
		of_platform_device_destroy(&pdle->pdev->dev, NULL);
		kfree(pdle);
	}
}
EXPORT_SYMBOL_GPL(onboard_hub_destroy_pdevs);
