/*
 * Media entity
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <media/media-entity.h>

/**
 * media_entity_init - Initialize a media entity
 *
 * @num_pads: Total number of sink and source pads.
 * @extra_links: Initial estimate of the number of extra links.
 * @pads: Array of 'num_pads' pads.
 *
 * The total number of pads is an intrinsic property of entities known by the
 * entity driver, while the total number of links depends on hardware design
 * and is an extrinsic property unknown to the entity driver. However, in most
 * use cases the entity driver can guess the number of links which can safely
 * be assumed to be equal to or larger than the number of pads.
 *
 * For those reasons the links array can be preallocated based on the entity
 * driver guess and will be reallocated later if extra links need to be
 * created.
 *
 * This function allocates a links array with enough space to hold at least
 * 'num_pads' + 'extra_links' elements. The media_entity::max_links field will
 * be set to the number of allocated elements.
 *
 * The pads array is managed by the entity driver and passed to
 * media_entity_init() where its pointer will be stored in the entity structure.
 */
int
media_entity_init(struct media_entity *entity, u16 num_pads,
		  struct media_pad *pads, u16 extra_links)
{
	struct media_link *links;
	unsigned int max_links = num_pads + extra_links;
	unsigned int i;

	links = kzalloc(max_links * sizeof(links[0]), GFP_KERNEL);
	if (links == NULL)
		return -ENOMEM;

	entity->group_id = 0;
	entity->max_links = max_links;
	entity->num_links = 0;
	entity->num_backlinks = 0;
	entity->num_pads = num_pads;
	entity->pads = pads;
	entity->links = links;

	for (i = 0; i < num_pads; i++) {
		pads[i].entity = entity;
		pads[i].index = i;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(media_entity_init);

void
media_entity_cleanup(struct media_entity *entity)
{
	kfree(entity->links);
}
EXPORT_SYMBOL_GPL(media_entity_cleanup);

static struct media_link *media_entity_add_link(struct media_entity *entity)
{
	if (entity->num_links >= entity->max_links) {
		struct media_link *links = entity->links;
		unsigned int max_links = entity->max_links + 2;
		unsigned int i;

		links = krealloc(links, max_links * sizeof(*links), GFP_KERNEL);
		if (links == NULL)
			return NULL;

		for (i = 0; i < entity->num_links; i++)
			links[i].reverse->reverse = &links[i];

		entity->max_links = max_links;
		entity->links = links;
	}

	return &entity->links[entity->num_links++];
}

int
media_entity_create_link(struct media_entity *source, u16 source_pad,
			 struct media_entity *sink, u16 sink_pad, u32 flags)
{
	struct media_link *link;
	struct media_link *backlink;

	BUG_ON(source == NULL || sink == NULL);
	BUG_ON(source_pad >= source->num_pads);
	BUG_ON(sink_pad >= sink->num_pads);

	link = media_entity_add_link(source);
	if (link == NULL)
		return -ENOMEM;

	link->source = &source->pads[source_pad];
	link->sink = &sink->pads[sink_pad];
	link->flags = flags;

	/* Create the backlink. Backlinks are used to help graph traversal and
	 * are not reported to userspace.
	 */
	backlink = media_entity_add_link(sink);
	if (backlink == NULL) {
		source->num_links--;
		return -ENOMEM;
	}

	backlink->source = &source->pads[source_pad];
	backlink->sink = &sink->pads[sink_pad];
	backlink->flags = flags;

	link->reverse = backlink;
	backlink->reverse = link;

	sink->num_backlinks++;

	return 0;
}
EXPORT_SYMBOL_GPL(media_entity_create_link);
