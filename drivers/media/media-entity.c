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

#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <media/media-entity.h>
#include <media/media-device.h>

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

/* -----------------------------------------------------------------------------
 * Graph traversal
 */

static struct media_entity *
media_entity_other(struct media_entity *entity, struct media_link *link)
{
	if (link->source->entity == entity)
		return link->sink->entity;
	else
		return link->source->entity;
}

/* push an entity to traversal stack */
static void stack_push(struct media_entity_graph *graph,
		       struct media_entity *entity)
{
	if (graph->top == MEDIA_ENTITY_ENUM_MAX_DEPTH - 1) {
		WARN_ON(1);
		return;
	}
	graph->top++;
	graph->stack[graph->top].link = 0;
	graph->stack[graph->top].entity = entity;
}

static struct media_entity *stack_pop(struct media_entity_graph *graph)
{
	struct media_entity *entity;

	entity = graph->stack[graph->top].entity;
	graph->top--;

	return entity;
}

#define link_top(en)	((en)->stack[(en)->top].link)
#define stack_top(en)	((en)->stack[(en)->top].entity)

/**
 * media_entity_graph_walk_start - Start walking the media graph at a given entity
 * @graph: Media graph structure that will be used to walk the graph
 * @entity: Starting entity
 *
 * This function initializes the graph traversal structure to walk the entities
 * graph starting at the given entity. The traversal structure must not be
 * modified by the caller during graph traversal. When done the structure can
 * safely be freed.
 */
void media_entity_graph_walk_start(struct media_entity_graph *graph,
				   struct media_entity *entity)
{
	graph->top = 0;
	graph->stack[graph->top].entity = NULL;
	bitmap_zero(graph->entities, MEDIA_ENTITY_ENUM_MAX_ID);

	if (WARN_ON(entity->id >= MEDIA_ENTITY_ENUM_MAX_ID))
		return;

	__set_bit(entity->id, graph->entities);
	stack_push(graph, entity);
}
EXPORT_SYMBOL_GPL(media_entity_graph_walk_start);

/**
 * media_entity_graph_walk_next - Get the next entity in the graph
 * @graph: Media graph structure
 *
 * Perform a depth-first traversal of the given media entities graph.
 *
 * The graph structure must have been previously initialized with a call to
 * media_entity_graph_walk_start().
 *
 * Return the next entity in the graph or NULL if the whole graph have been
 * traversed.
 */
struct media_entity *
media_entity_graph_walk_next(struct media_entity_graph *graph)
{
	if (stack_top(graph) == NULL)
		return NULL;

	/*
	 * Depth first search. Push entity to stack and continue from
	 * top of the stack until no more entities on the level can be
	 * found.
	 */
	while (link_top(graph) < stack_top(graph)->num_links) {
		struct media_entity *entity = stack_top(graph);
		struct media_link *link = &entity->links[link_top(graph)];
		struct media_entity *next;

		/* The link is not enabled so we do not follow. */
		if (!(link->flags & MEDIA_LNK_FL_ENABLED)) {
			link_top(graph)++;
			continue;
		}

		/* Get the entity in the other end of the link . */
		next = media_entity_other(entity, link);
		if (WARN_ON(next->id >= MEDIA_ENTITY_ENUM_MAX_ID))
			return NULL;

		/* Has the entity already been visited? */
		if (__test_and_set_bit(next->id, graph->entities)) {
			link_top(graph)++;
			continue;
		}

		/* Push the new entity to stack and start over. */
		link_top(graph)++;
		stack_push(graph, next);
	}

	return stack_pop(graph);
}
EXPORT_SYMBOL_GPL(media_entity_graph_walk_next);

/* -----------------------------------------------------------------------------
 * Pipeline management
 */

/**
 * media_entity_pipeline_start - Mark a pipeline as streaming
 * @entity: Starting entity
 * @pipe: Media pipeline to be assigned to all entities in the pipeline.
 *
 * Mark all entities connected to a given entity through enabled links, either
 * directly or indirectly, as streaming. The given pipeline object is assigned to
 * every entity in the pipeline and stored in the media_entity pipe field.
 *
 * Calls to this function can be nested, in which case the same number of
 * media_entity_pipeline_stop() calls will be required to stop streaming. The
 * pipeline pointer must be identical for all nested calls to
 * media_entity_pipeline_start().
 */
__must_check int media_entity_pipeline_start(struct media_entity *entity,
					     struct media_pipeline *pipe)
{
	struct media_device *mdev = entity->parent;
	struct media_entity_graph graph;
	struct media_entity *entity_err = entity;
	int ret;

	mutex_lock(&mdev->graph_mutex);

	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		DECLARE_BITMAP(active, entity->num_pads);
		DECLARE_BITMAP(has_no_links, entity->num_pads);
		unsigned int i;

		entity->stream_count++;
		WARN_ON(entity->pipe && entity->pipe != pipe);
		entity->pipe = pipe;

		/* Already streaming --- no need to check. */
		if (entity->stream_count > 1)
			continue;

		if (!entity->ops || !entity->ops->link_validate)
			continue;

		bitmap_zero(active, entity->num_pads);
		bitmap_fill(has_no_links, entity->num_pads);

		for (i = 0; i < entity->num_links; i++) {
			struct media_link *link = &entity->links[i];
			struct media_pad *pad = link->sink->entity == entity
						? link->sink : link->source;

			/* Mark that a pad is connected by a link. */
			bitmap_clear(has_no_links, pad->index, 1);

			/*
			 * Pads that either do not need to connect or
			 * are connected through an enabled link are
			 * fine.
			 */
			if (!(pad->flags & MEDIA_PAD_FL_MUST_CONNECT) ||
			    link->flags & MEDIA_LNK_FL_ENABLED)
				bitmap_set(active, pad->index, 1);

			/*
			 * Link validation will only take place for
			 * sink ends of the link that are enabled.
			 */
			if (link->sink != pad ||
			    !(link->flags & MEDIA_LNK_FL_ENABLED))
				continue;

			ret = entity->ops->link_validate(link);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				goto error;
		}

		/* Either no links or validated links are fine. */
		bitmap_or(active, active, has_no_links, entity->num_pads);

		if (!bitmap_full(active, entity->num_pads)) {
			ret = -EPIPE;
			goto error;
		}
	}

	mutex_unlock(&mdev->graph_mutex);

	return 0;

error:
	/*
	 * Link validation on graph failed. We revert what we did and
	 * return the error.
	 */
	media_entity_graph_walk_start(&graph, entity_err);

	while ((entity_err = media_entity_graph_walk_next(&graph))) {
		entity_err->stream_count--;
		if (entity_err->stream_count == 0)
			entity_err->pipe = NULL;

		/*
		 * We haven't increased stream_count further than this
		 * so we quit here.
		 */
		if (entity_err == entity)
			break;
	}

	mutex_unlock(&mdev->graph_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(media_entity_pipeline_start);

/**
 * media_entity_pipeline_stop - Mark a pipeline as not streaming
 * @entity: Starting entity
 *
 * Mark all entities connected to a given entity through enabled links, either
 * directly or indirectly, as not streaming. The media_entity pipe field is
 * reset to NULL.
 *
 * If multiple calls to media_entity_pipeline_start() have been made, the same
 * number of calls to this function are required to mark the pipeline as not
 * streaming.
 */
void media_entity_pipeline_stop(struct media_entity *entity)
{
	struct media_device *mdev = entity->parent;
	struct media_entity_graph graph;

	mutex_lock(&mdev->graph_mutex);

	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		entity->stream_count--;
		if (entity->stream_count == 0)
			entity->pipe = NULL;
	}

	mutex_unlock(&mdev->graph_mutex);
}
EXPORT_SYMBOL_GPL(media_entity_pipeline_stop);

/* -----------------------------------------------------------------------------
 * Module use count
 */

/*
 * media_entity_get - Get a reference to the parent module
 * @entity: The entity
 *
 * Get a reference to the parent media device module.
 *
 * The function will return immediately if @entity is NULL.
 *
 * Return a pointer to the entity on success or NULL on failure.
 */
struct media_entity *media_entity_get(struct media_entity *entity)
{
	if (entity == NULL)
		return NULL;

	if (entity->parent->dev &&
	    !try_module_get(entity->parent->dev->driver->owner))
		return NULL;

	return entity;
}
EXPORT_SYMBOL_GPL(media_entity_get);

/*
 * media_entity_put - Release the reference to the parent module
 * @entity: The entity
 *
 * Release the reference count acquired by media_entity_get().
 *
 * The function will return immediately if @entity is NULL.
 */
void media_entity_put(struct media_entity *entity)
{
	if (entity == NULL)
		return;

	if (entity->parent->dev)
		module_put(entity->parent->dev->driver->owner);
}
EXPORT_SYMBOL_GPL(media_entity_put);

/* -----------------------------------------------------------------------------
 * Links management
 */

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

void __media_entity_remove_links(struct media_entity *entity)
{
	unsigned int i;

	for (i = 0; i < entity->num_links; i++) {
		struct media_link *link = &entity->links[i];
		struct media_entity *remote;
		unsigned int r = 0;

		if (link->source->entity == entity)
			remote = link->sink->entity;
		else
			remote = link->source->entity;

		while (r < remote->num_links) {
			struct media_link *rlink = &remote->links[r];

			if (rlink != link->reverse) {
				r++;
				continue;
			}

			if (link->source->entity == entity)
				remote->num_backlinks--;

			if (--remote->num_links == 0)
				break;

			/* Insert last entry in place of the dropped link. */
			*rlink = remote->links[remote->num_links];
		}
	}

	entity->num_links = 0;
	entity->num_backlinks = 0;
}
EXPORT_SYMBOL_GPL(__media_entity_remove_links);

void media_entity_remove_links(struct media_entity *entity)
{
	/* Do nothing if the entity is not registered. */
	if (entity->parent == NULL)
		return;

	mutex_lock(&entity->parent->graph_mutex);
	__media_entity_remove_links(entity);
	mutex_unlock(&entity->parent->graph_mutex);
}
EXPORT_SYMBOL_GPL(media_entity_remove_links);

static int __media_entity_setup_link_notify(struct media_link *link, u32 flags)
{
	int ret;

	/* Notify both entities. */
	ret = media_entity_call(link->source->entity, link_setup,
				link->source, link->sink, flags);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	ret = media_entity_call(link->sink->entity, link_setup,
				link->sink, link->source, flags);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		media_entity_call(link->source->entity, link_setup,
				  link->source, link->sink, link->flags);
		return ret;
	}

	link->flags = flags;
	link->reverse->flags = link->flags;

	return 0;
}

/**
 * __media_entity_setup_link - Configure a media link
 * @link: The link being configured
 * @flags: Link configuration flags
 *
 * The bulk of link setup is handled by the two entities connected through the
 * link. This function notifies both entities of the link configuration change.
 *
 * If the link is immutable or if the current and new configuration are
 * identical, return immediately.
 *
 * The user is expected to hold link->source->parent->mutex. If not,
 * media_entity_setup_link() should be used instead.
 */
int __media_entity_setup_link(struct media_link *link, u32 flags)
{
	const u32 mask = MEDIA_LNK_FL_ENABLED;
	struct media_device *mdev;
	struct media_entity *source, *sink;
	int ret = -EBUSY;

	if (link == NULL)
		return -EINVAL;

	/* The non-modifiable link flags must not be modified. */
	if ((link->flags & ~mask) != (flags & ~mask))
		return -EINVAL;

	if (link->flags & MEDIA_LNK_FL_IMMUTABLE)
		return link->flags == flags ? 0 : -EINVAL;

	if (link->flags == flags)
		return 0;

	source = link->source->entity;
	sink = link->sink->entity;

	if (!(link->flags & MEDIA_LNK_FL_DYNAMIC) &&
	    (source->stream_count || sink->stream_count))
		return -EBUSY;

	mdev = source->parent;

	if (mdev->link_notify) {
		ret = mdev->link_notify(link, flags,
					MEDIA_DEV_NOTIFY_PRE_LINK_CH);
		if (ret < 0)
			return ret;
	}

	ret = __media_entity_setup_link_notify(link, flags);

	if (mdev->link_notify)
		mdev->link_notify(link, flags, MEDIA_DEV_NOTIFY_POST_LINK_CH);

	return ret;
}

int media_entity_setup_link(struct media_link *link, u32 flags)
{
	int ret;

	mutex_lock(&link->source->entity->parent->graph_mutex);
	ret = __media_entity_setup_link(link, flags);
	mutex_unlock(&link->source->entity->parent->graph_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(media_entity_setup_link);

/**
 * media_entity_find_link - Find a link between two pads
 * @source: Source pad
 * @sink: Sink pad
 *
 * Return a pointer to the link between the two entities. If no such link
 * exists, return NULL.
 */
struct media_link *
media_entity_find_link(struct media_pad *source, struct media_pad *sink)
{
	struct media_link *link;
	unsigned int i;

	for (i = 0; i < source->entity->num_links; ++i) {
		link = &source->entity->links[i];

		if (link->source->entity == source->entity &&
		    link->source->index == source->index &&
		    link->sink->entity == sink->entity &&
		    link->sink->index == sink->index)
			return link;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(media_entity_find_link);

/**
 * media_entity_remote_pad - Find the pad at the remote end of a link
 * @pad: Pad at the local end of the link
 *
 * Search for a remote pad connected to the given pad by iterating over all
 * links originating or terminating at that pad until an enabled link is found.
 *
 * Return a pointer to the pad at the remote end of the first found enabled
 * link, or NULL if no enabled link has been found.
 */
struct media_pad *media_entity_remote_pad(struct media_pad *pad)
{
	unsigned int i;

	for (i = 0; i < pad->entity->num_links; i++) {
		struct media_link *link = &pad->entity->links[i];

		if (!(link->flags & MEDIA_LNK_FL_ENABLED))
			continue;

		if (link->source == pad)
			return link->sink;

		if (link->sink == pad)
			return link->source;
	}

	return NULL;

}
EXPORT_SYMBOL_GPL(media_entity_remote_pad);
