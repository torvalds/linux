// SPDX-License-Identifier: GPL-2.0-only
/*
 * Media entity
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 */

#include <linux/bitmap.h>
#include <linux/list.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <media/media-entity.h>
#include <media/media-device.h>

static inline const char *intf_type(struct media_interface *intf)
{
	switch (intf->type) {
	case MEDIA_INTF_T_DVB_FE:
		return "dvb-frontend";
	case MEDIA_INTF_T_DVB_DEMUX:
		return "dvb-demux";
	case MEDIA_INTF_T_DVB_DVR:
		return "dvb-dvr";
	case MEDIA_INTF_T_DVB_CA:
		return  "dvb-ca";
	case MEDIA_INTF_T_DVB_NET:
		return "dvb-net";
	case MEDIA_INTF_T_V4L_VIDEO:
		return "v4l-video";
	case MEDIA_INTF_T_V4L_VBI:
		return "v4l-vbi";
	case MEDIA_INTF_T_V4L_RADIO:
		return "v4l-radio";
	case MEDIA_INTF_T_V4L_SUBDEV:
		return "v4l-subdev";
	case MEDIA_INTF_T_V4L_SWRADIO:
		return "v4l-swradio";
	case MEDIA_INTF_T_V4L_TOUCH:
		return "v4l-touch";
	default:
		return "unknown-intf";
	}
};

static inline const char *link_type_name(struct media_link *link)
{
	switch (link->flags & MEDIA_LNK_FL_LINK_TYPE) {
	case MEDIA_LNK_FL_DATA_LINK:
		return "data";
	case MEDIA_LNK_FL_INTERFACE_LINK:
		return "interface";
	case MEDIA_LNK_FL_ANCILLARY_LINK:
		return "ancillary";
	default:
		return "unknown";
	}
}

__must_check int media_entity_enum_init(struct media_entity_enum *ent_enum,
					struct media_device *mdev)
{
	int idx_max;

	idx_max = ALIGN(mdev->entity_internal_idx_max + 1, BITS_PER_LONG);
	ent_enum->bmap = bitmap_zalloc(idx_max, GFP_KERNEL);
	if (!ent_enum->bmap)
		return -ENOMEM;

	ent_enum->idx_max = idx_max;

	return 0;
}
EXPORT_SYMBOL_GPL(media_entity_enum_init);

void media_entity_enum_cleanup(struct media_entity_enum *ent_enum)
{
	bitmap_free(ent_enum->bmap);
}
EXPORT_SYMBOL_GPL(media_entity_enum_cleanup);

/**
 *  dev_dbg_obj - Prints in debug mode a change on some object
 *
 * @event_name:	Name of the event to report. Could be __func__
 * @gobj:	Pointer to the object
 *
 * Enabled only if DEBUG or CONFIG_DYNAMIC_DEBUG. Otherwise, it
 * won't produce any code.
 */
static void dev_dbg_obj(const char *event_name,  struct media_gobj *gobj)
{
#if defined(DEBUG) || defined (CONFIG_DYNAMIC_DEBUG)
	switch (media_type(gobj)) {
	case MEDIA_GRAPH_ENTITY:
		dev_dbg(gobj->mdev->dev,
			"%s id %u: entity '%s'\n",
			event_name, media_id(gobj),
			gobj_to_entity(gobj)->name);
		break;
	case MEDIA_GRAPH_LINK:
	{
		struct media_link *link = gobj_to_link(gobj);

		dev_dbg(gobj->mdev->dev,
			"%s id %u: %s link id %u ==> id %u\n",
			event_name, media_id(gobj), link_type_name(link),
			media_id(link->gobj0),
			media_id(link->gobj1));
		break;
	}
	case MEDIA_GRAPH_PAD:
	{
		struct media_pad *pad = gobj_to_pad(gobj);

		dev_dbg(gobj->mdev->dev,
			"%s id %u: %s%spad '%s':%d\n",
			event_name, media_id(gobj),
			pad->flags & MEDIA_PAD_FL_SINK   ? "sink " : "",
			pad->flags & MEDIA_PAD_FL_SOURCE ? "source " : "",
			pad->entity->name, pad->index);
		break;
	}
	case MEDIA_GRAPH_INTF_DEVNODE:
	{
		struct media_interface *intf = gobj_to_intf(gobj);
		struct media_intf_devnode *devnode = intf_to_devnode(intf);

		dev_dbg(gobj->mdev->dev,
			"%s id %u: intf_devnode %s - major: %d, minor: %d\n",
			event_name, media_id(gobj),
			intf_type(intf),
			devnode->major, devnode->minor);
		break;
	}
	}
#endif
}

void media_gobj_create(struct media_device *mdev,
			   enum media_gobj_type type,
			   struct media_gobj *gobj)
{
	BUG_ON(!mdev);

	gobj->mdev = mdev;

	/* Create a per-type unique object ID */
	gobj->id = media_gobj_gen_id(type, ++mdev->id);

	switch (type) {
	case MEDIA_GRAPH_ENTITY:
		list_add_tail(&gobj->list, &mdev->entities);
		break;
	case MEDIA_GRAPH_PAD:
		list_add_tail(&gobj->list, &mdev->pads);
		break;
	case MEDIA_GRAPH_LINK:
		list_add_tail(&gobj->list, &mdev->links);
		break;
	case MEDIA_GRAPH_INTF_DEVNODE:
		list_add_tail(&gobj->list, &mdev->interfaces);
		break;
	}

	mdev->topology_version++;

	dev_dbg_obj(__func__, gobj);
}

void media_gobj_destroy(struct media_gobj *gobj)
{
	/* Do nothing if the object is not linked. */
	if (gobj->mdev == NULL)
		return;

	dev_dbg_obj(__func__, gobj);

	gobj->mdev->topology_version++;

	/* Remove the object from mdev list */
	list_del(&gobj->list);

	gobj->mdev = NULL;
}

/*
 * TODO: Get rid of this.
 */
#define MEDIA_ENTITY_MAX_PADS		512

int media_entity_pads_init(struct media_entity *entity, u16 num_pads,
			   struct media_pad *pads)
{
	struct media_device *mdev = entity->graph_obj.mdev;
	struct media_pad *iter;
	unsigned int i = 0;

	if (num_pads >= MEDIA_ENTITY_MAX_PADS)
		return -E2BIG;

	entity->num_pads = num_pads;
	entity->pads = pads;

	if (mdev)
		mutex_lock(&mdev->graph_mutex);

	media_entity_for_each_pad(entity, iter) {
		iter->entity = entity;
		iter->index = i++;
		if (mdev)
			media_gobj_create(mdev, MEDIA_GRAPH_PAD,
					  &iter->graph_obj);
	}

	if (mdev)
		mutex_unlock(&mdev->graph_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(media_entity_pads_init);

/* -----------------------------------------------------------------------------
 * Graph traversal
 */

/*
 * This function checks the interdependency inside the entity between @pad0
 * and @pad1. If two pads are interdependent they are part of the same pipeline
 * and enabling one of the pads means that the other pad will become "locked"
 * and doesn't allow configuration changes.
 *
 * This function uses the &media_entity_operations.has_pad_interdep() operation
 * to check the dependency inside the entity between @pad0 and @pad1. If the
 * has_pad_interdep operation is not implemented, all pads of the entity are
 * considered to be interdependent.
 */
static bool media_entity_has_pad_interdep(struct media_entity *entity,
					  unsigned int pad0, unsigned int pad1)
{
	if (pad0 >= entity->num_pads || pad1 >= entity->num_pads)
		return false;

	if (entity->pads[pad0].flags & entity->pads[pad1].flags &
	    (MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_SOURCE))
		return false;

	if (!entity->ops || !entity->ops->has_pad_interdep)
		return true;

	return entity->ops->has_pad_interdep(entity, pad0, pad1);
}

static struct media_entity *
media_entity_other(struct media_entity *entity, struct media_link *link)
{
	if (link->source->entity == entity)
		return link->sink->entity;
	else
		return link->source->entity;
}

/* push an entity to traversal stack */
static void stack_push(struct media_graph *graph,
		       struct media_entity *entity)
{
	if (graph->top == MEDIA_ENTITY_ENUM_MAX_DEPTH - 1) {
		WARN_ON(1);
		return;
	}
	graph->top++;
	graph->stack[graph->top].link = entity->links.next;
	graph->stack[graph->top].entity = entity;
}

static struct media_entity *stack_pop(struct media_graph *graph)
{
	struct media_entity *entity;

	entity = graph->stack[graph->top].entity;
	graph->top--;

	return entity;
}

#define link_top(en)	((en)->stack[(en)->top].link)
#define stack_top(en)	((en)->stack[(en)->top].entity)

/**
 * media_graph_walk_init - Allocate resources for graph walk
 * @graph: Media graph structure that will be used to walk the graph
 * @mdev: Media device
 *
 * Reserve resources for graph walk in media device's current
 * state. The memory must be released using
 * media_graph_walk_free().
 *
 * Returns error on failure, zero on success.
 */
__must_check int media_graph_walk_init(
	struct media_graph *graph, struct media_device *mdev)
{
	return media_entity_enum_init(&graph->ent_enum, mdev);
}
EXPORT_SYMBOL_GPL(media_graph_walk_init);

/**
 * media_graph_walk_cleanup - Release resources related to graph walking
 * @graph: Media graph structure that was used to walk the graph
 */
void media_graph_walk_cleanup(struct media_graph *graph)
{
	media_entity_enum_cleanup(&graph->ent_enum);
}
EXPORT_SYMBOL_GPL(media_graph_walk_cleanup);

void media_graph_walk_start(struct media_graph *graph,
			    struct media_entity *entity)
{
	media_entity_enum_zero(&graph->ent_enum);
	media_entity_enum_set(&graph->ent_enum, entity);

	graph->top = 0;
	graph->stack[graph->top].entity = NULL;
	stack_push(graph, entity);
	dev_dbg(entity->graph_obj.mdev->dev,
		"begin graph walk at '%s'\n", entity->name);
}
EXPORT_SYMBOL_GPL(media_graph_walk_start);

static void media_graph_walk_iter(struct media_graph *graph)
{
	struct media_entity *entity = stack_top(graph);
	struct media_link *link;
	struct media_entity *next;

	link = list_entry(link_top(graph), typeof(*link), list);

	/* If the link is not a data link, don't follow it */
	if ((link->flags & MEDIA_LNK_FL_LINK_TYPE) != MEDIA_LNK_FL_DATA_LINK) {
		link_top(graph) = link_top(graph)->next;
		return;
	}

	/* The link is not enabled so we do not follow. */
	if (!(link->flags & MEDIA_LNK_FL_ENABLED)) {
		link_top(graph) = link_top(graph)->next;
		dev_dbg(entity->graph_obj.mdev->dev,
			"walk: skipping disabled link '%s':%u -> '%s':%u\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index);
		return;
	}

	/* Get the entity at the other end of the link. */
	next = media_entity_other(entity, link);

	/* Has the entity already been visited? */
	if (media_entity_enum_test_and_set(&graph->ent_enum, next)) {
		link_top(graph) = link_top(graph)->next;
		dev_dbg(entity->graph_obj.mdev->dev,
			"walk: skipping entity '%s' (already seen)\n",
			next->name);
		return;
	}

	/* Push the new entity to stack and start over. */
	link_top(graph) = link_top(graph)->next;
	stack_push(graph, next);
	dev_dbg(entity->graph_obj.mdev->dev, "walk: pushing '%s' on stack\n",
		next->name);
	lockdep_assert_held(&entity->graph_obj.mdev->graph_mutex);
}

struct media_entity *media_graph_walk_next(struct media_graph *graph)
{
	struct media_entity *entity;

	if (stack_top(graph) == NULL)
		return NULL;

	/*
	 * Depth first search. Push entity to stack and continue from
	 * top of the stack until no more entities on the level can be
	 * found.
	 */
	while (link_top(graph) != &stack_top(graph)->links)
		media_graph_walk_iter(graph);

	entity = stack_pop(graph);
	dev_dbg(entity->graph_obj.mdev->dev,
		"walk: returning entity '%s'\n", entity->name);

	return entity;
}
EXPORT_SYMBOL_GPL(media_graph_walk_next);

/* -----------------------------------------------------------------------------
 * Pipeline management
 */

/*
 * The pipeline traversal stack stores pads that are reached during graph
 * traversal, with a list of links to be visited to continue the traversal.
 * When a new pad is reached, an entry is pushed on the top of the stack and
 * points to the incoming pad and the first link of the entity.
 *
 * To find further pads in the pipeline, the traversal algorithm follows
 * internal pad dependencies in the entity, and then links in the graph. It
 * does so by iterating over all links of the entity, and following enabled
 * links that originate from a pad that is internally connected to the incoming
 * pad, as reported by the media_entity_has_pad_interdep() function.
 */

/**
 * struct media_pipeline_walk_entry - Entry in the pipeline traversal stack
 *
 * @pad: The media pad being visited
 * @links: Links left to be visited
 */
struct media_pipeline_walk_entry {
	struct media_pad *pad;
	struct list_head *links;
};

/**
 * struct media_pipeline_walk - State used by the media pipeline traversal
 *				algorithm
 *
 * @mdev: The media device
 * @stack: Depth-first search stack
 * @stack.size: Number of allocated entries in @stack.entries
 * @stack.top: Index of the top stack entry (-1 if the stack is empty)
 * @stack.entries: Stack entries
 */
struct media_pipeline_walk {
	struct media_device *mdev;

	struct {
		unsigned int size;
		int top;
		struct media_pipeline_walk_entry *entries;
	} stack;
};

#define MEDIA_PIPELINE_STACK_GROW_STEP		16

static struct media_pipeline_walk_entry *
media_pipeline_walk_top(struct media_pipeline_walk *walk)
{
	return &walk->stack.entries[walk->stack.top];
}

static bool media_pipeline_walk_empty(struct media_pipeline_walk *walk)
{
	return walk->stack.top == -1;
}

/* Increase the stack size by MEDIA_PIPELINE_STACK_GROW_STEP elements. */
static int media_pipeline_walk_resize(struct media_pipeline_walk *walk)
{
	struct media_pipeline_walk_entry *entries;
	unsigned int new_size;

	/* Safety check, to avoid stack overflows in case of bugs. */
	if (walk->stack.size >= 256)
		return -E2BIG;

	new_size = walk->stack.size + MEDIA_PIPELINE_STACK_GROW_STEP;

	entries = krealloc(walk->stack.entries,
			   new_size * sizeof(*walk->stack.entries),
			   GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	walk->stack.entries = entries;
	walk->stack.size = new_size;

	return 0;
}

/* Push a new entry on the stack. */
static int media_pipeline_walk_push(struct media_pipeline_walk *walk,
				    struct media_pad *pad)
{
	struct media_pipeline_walk_entry *entry;
	int ret;

	if (walk->stack.top + 1 >= walk->stack.size) {
		ret = media_pipeline_walk_resize(walk);
		if (ret)
			return ret;
	}

	walk->stack.top++;
	entry = media_pipeline_walk_top(walk);
	entry->pad = pad;
	entry->links = pad->entity->links.next;

	dev_dbg(walk->mdev->dev,
		"media pipeline: pushed entry %u: '%s':%u\n",
		walk->stack.top, pad->entity->name, pad->index);

	return 0;
}

/*
 * Move the top entry link cursor to the next link. If all links of the entry
 * have been visited, pop the entry itself.
 */
static void media_pipeline_walk_pop(struct media_pipeline_walk *walk)
{
	struct media_pipeline_walk_entry *entry;

	if (WARN_ON(walk->stack.top < 0))
		return;

	entry = media_pipeline_walk_top(walk);

	if (entry->links->next == &entry->pad->entity->links) {
		dev_dbg(walk->mdev->dev,
			"media pipeline: entry %u has no more links, popping\n",
			walk->stack.top);

		walk->stack.top--;
		return;
	}

	entry->links = entry->links->next;

	dev_dbg(walk->mdev->dev,
		"media pipeline: moved entry %u to next link\n",
		walk->stack.top);
}

/* Free all memory allocated while walking the pipeline. */
static void media_pipeline_walk_destroy(struct media_pipeline_walk *walk)
{
	kfree(walk->stack.entries);
}

/* Add a pad to the pipeline and push it to the stack. */
static int media_pipeline_add_pad(struct media_pipeline *pipe,
				  struct media_pipeline_walk *walk,
				  struct media_pad *pad)
{
	struct media_pipeline_pad *ppad;

	list_for_each_entry(ppad, &pipe->pads, list) {
		if (ppad->pad == pad) {
			dev_dbg(pad->graph_obj.mdev->dev,
				"media pipeline: already contains pad '%s':%u\n",
				pad->entity->name, pad->index);
			return 0;
		}
	}

	ppad = kzalloc(sizeof(*ppad), GFP_KERNEL);
	if (!ppad)
		return -ENOMEM;

	ppad->pipe = pipe;
	ppad->pad = pad;

	list_add_tail(&ppad->list, &pipe->pads);

	dev_dbg(pad->graph_obj.mdev->dev,
		"media pipeline: added pad '%s':%u\n",
		pad->entity->name, pad->index);

	return media_pipeline_walk_push(walk, pad);
}

/* Explore the next link of the entity at the top of the stack. */
static int media_pipeline_explore_next_link(struct media_pipeline *pipe,
					    struct media_pipeline_walk *walk)
{
	struct media_pipeline_walk_entry *entry = media_pipeline_walk_top(walk);
	struct media_pad *pad;
	struct media_link *link;
	struct media_pad *local;
	struct media_pad *remote;
	int ret;

	pad = entry->pad;
	link = list_entry(entry->links, typeof(*link), list);
	media_pipeline_walk_pop(walk);

	dev_dbg(walk->mdev->dev,
		"media pipeline: exploring link '%s':%u -> '%s':%u\n",
		link->source->entity->name, link->source->index,
		link->sink->entity->name, link->sink->index);

	/* Skip links that are not enabled. */
	if (!(link->flags & MEDIA_LNK_FL_ENABLED)) {
		dev_dbg(walk->mdev->dev,
			"media pipeline: skipping link (disabled)\n");
		return 0;
	}

	/* Get the local pad and remote pad. */
	if (link->source->entity == pad->entity) {
		local = link->source;
		remote = link->sink;
	} else {
		local = link->sink;
		remote = link->source;
	}

	/*
	 * Skip links that originate from a different pad than the incoming pad
	 * that is not connected internally in the entity to the incoming pad.
	 */
	if (pad != local &&
	    !media_entity_has_pad_interdep(pad->entity, pad->index, local->index)) {
		dev_dbg(walk->mdev->dev,
			"media pipeline: skipping link (no route)\n");
		return 0;
	}

	/*
	 * Add the local and remote pads of the link to the pipeline and push
	 * them to the stack, if they're not already present.
	 */
	ret = media_pipeline_add_pad(pipe, walk, local);
	if (ret)
		return ret;

	ret = media_pipeline_add_pad(pipe, walk, remote);
	if (ret)
		return ret;

	return 0;
}

static void media_pipeline_cleanup(struct media_pipeline *pipe)
{
	while (!list_empty(&pipe->pads)) {
		struct media_pipeline_pad *ppad;

		ppad = list_first_entry(&pipe->pads, typeof(*ppad), list);
		list_del(&ppad->list);
		kfree(ppad);
	}
}

static int media_pipeline_populate(struct media_pipeline *pipe,
				   struct media_pad *pad)
{
	struct media_pipeline_walk walk = { };
	struct media_pipeline_pad *ppad;
	int ret;

	/*
	 * Populate the media pipeline by walking the media graph, starting
	 * from @pad.
	 */
	INIT_LIST_HEAD(&pipe->pads);
	pipe->mdev = pad->graph_obj.mdev;

	walk.mdev = pipe->mdev;
	walk.stack.top = -1;
	ret = media_pipeline_add_pad(pipe, &walk, pad);
	if (ret)
		goto done;

	/*
	 * Use a depth-first search algorithm: as long as the stack is not
	 * empty, explore the next link of the top entry. The
	 * media_pipeline_explore_next_link() function will either move to the
	 * next link, pop the entry if fully visited, or add new entries on
	 * top.
	 */
	while (!media_pipeline_walk_empty(&walk)) {
		ret = media_pipeline_explore_next_link(pipe, &walk);
		if (ret)
			goto done;
	}

	dev_dbg(pad->graph_obj.mdev->dev,
		"media pipeline populated, found pads:\n");

	list_for_each_entry(ppad, &pipe->pads, list)
		dev_dbg(pad->graph_obj.mdev->dev, "- '%s':%u\n",
			ppad->pad->entity->name, ppad->pad->index);

	WARN_ON(walk.stack.top != -1);

	ret = 0;

done:
	media_pipeline_walk_destroy(&walk);

	if (ret)
		media_pipeline_cleanup(pipe);

	return ret;
}

__must_check int __media_pipeline_start(struct media_pad *pad,
					struct media_pipeline *pipe)
{
	struct media_device *mdev = pad->entity->graph_obj.mdev;
	struct media_pipeline_pad *err_ppad;
	struct media_pipeline_pad *ppad;
	int ret;

	lockdep_assert_held(&mdev->graph_mutex);

	/*
	 * If the entity is already part of a pipeline, that pipeline must
	 * be the same as the pipe given to media_pipeline_start().
	 */
	if (WARN_ON(pad->pipe && pad->pipe != pipe))
		return -EINVAL;

	/*
	 * If the pipeline has already been started, it is guaranteed to be
	 * valid, so just increase the start count.
	 */
	if (pipe->start_count) {
		pipe->start_count++;
		return 0;
	}

	/*
	 * Populate the pipeline. This populates the media_pipeline pads list
	 * with media_pipeline_pad instances for each pad found during graph
	 * walk.
	 */
	ret = media_pipeline_populate(pipe, pad);
	if (ret)
		return ret;

	/*
	 * Now that all the pads in the pipeline have been gathered, perform
	 * the validation steps.
	 */

	list_for_each_entry(ppad, &pipe->pads, list) {
		struct media_pad *pad = ppad->pad;
		struct media_entity *entity = pad->entity;
		bool has_enabled_link = false;
		bool has_link = false;
		struct media_link *link;

		dev_dbg(mdev->dev, "Validating pad '%s':%u\n", pad->entity->name,
			pad->index);

		/*
		 * 1. Ensure that the pad doesn't already belong to a different
		 * pipeline.
		 */
		if (pad->pipe) {
			dev_dbg(mdev->dev, "Failed to start pipeline: pad '%s':%u busy\n",
				pad->entity->name, pad->index);
			ret = -EBUSY;
			goto error;
		}

		/*
		 * 2. Validate all active links whose sink is the current pad.
		 * Validation of the source pads is performed in the context of
		 * the connected sink pad to avoid duplicating checks.
		 */
		for_each_media_entity_data_link(entity, link) {
			/* Skip links unrelated to the current pad. */
			if (link->sink != pad && link->source != pad)
				continue;

			/* Record if the pad has links and enabled links. */
			if (link->flags & MEDIA_LNK_FL_ENABLED)
				has_enabled_link = true;
			has_link = true;

			/*
			 * Validate the link if it's enabled and has the
			 * current pad as its sink.
			 */
			if (!(link->flags & MEDIA_LNK_FL_ENABLED))
				continue;

			if (link->sink != pad)
				continue;

			if (!entity->ops || !entity->ops->link_validate)
				continue;

			ret = entity->ops->link_validate(link);
			if (ret) {
				dev_dbg(mdev->dev,
					"Link '%s':%u -> '%s':%u failed validation: %d\n",
					link->source->entity->name,
					link->source->index,
					link->sink->entity->name,
					link->sink->index, ret);
				goto error;
			}

			dev_dbg(mdev->dev,
				"Link '%s':%u -> '%s':%u is valid\n",
				link->source->entity->name,
				link->source->index,
				link->sink->entity->name,
				link->sink->index);
		}

		/*
		 * 3. If the pad has the MEDIA_PAD_FL_MUST_CONNECT flag set,
		 * ensure that it has either no link or an enabled link.
		 */
		if ((pad->flags & MEDIA_PAD_FL_MUST_CONNECT) && has_link &&
		    !has_enabled_link) {
			dev_dbg(mdev->dev,
				"Pad '%s':%u must be connected by an enabled link\n",
				pad->entity->name, pad->index);
			ret = -ENOLINK;
			goto error;
		}

		/* Validation passed, store the pipe pointer in the pad. */
		pad->pipe = pipe;
	}

	pipe->start_count++;

	return 0;

error:
	/*
	 * Link validation on graph failed. We revert what we did and
	 * return the error.
	 */

	list_for_each_entry(err_ppad, &pipe->pads, list) {
		if (err_ppad == ppad)
			break;

		err_ppad->pad->pipe = NULL;
	}

	media_pipeline_cleanup(pipe);

	return ret;
}
EXPORT_SYMBOL_GPL(__media_pipeline_start);

__must_check int media_pipeline_start(struct media_pad *pad,
				      struct media_pipeline *pipe)
{
	struct media_device *mdev = pad->entity->graph_obj.mdev;
	int ret;

	mutex_lock(&mdev->graph_mutex);
	ret = __media_pipeline_start(pad, pipe);
	mutex_unlock(&mdev->graph_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(media_pipeline_start);

void __media_pipeline_stop(struct media_pad *pad)
{
	struct media_pipeline *pipe = pad->pipe;
	struct media_pipeline_pad *ppad;

	/*
	 * If the following check fails, the driver has performed an
	 * unbalanced call to media_pipeline_stop()
	 */
	if (WARN_ON(!pipe))
		return;

	if (--pipe->start_count)
		return;

	list_for_each_entry(ppad, &pipe->pads, list)
		ppad->pad->pipe = NULL;

	media_pipeline_cleanup(pipe);

	if (pipe->allocated)
		kfree(pipe);
}
EXPORT_SYMBOL_GPL(__media_pipeline_stop);

void media_pipeline_stop(struct media_pad *pad)
{
	struct media_device *mdev = pad->entity->graph_obj.mdev;

	mutex_lock(&mdev->graph_mutex);
	__media_pipeline_stop(pad);
	mutex_unlock(&mdev->graph_mutex);
}
EXPORT_SYMBOL_GPL(media_pipeline_stop);

__must_check int media_pipeline_alloc_start(struct media_pad *pad)
{
	struct media_device *mdev = pad->entity->graph_obj.mdev;
	struct media_pipeline *new_pipe = NULL;
	struct media_pipeline *pipe;
	int ret;

	mutex_lock(&mdev->graph_mutex);

	/*
	 * Is the entity already part of a pipeline? If not, we need to allocate
	 * a pipe.
	 */
	pipe = media_pad_pipeline(pad);
	if (!pipe) {
		new_pipe = kzalloc(sizeof(*new_pipe), GFP_KERNEL);
		if (!new_pipe) {
			ret = -ENOMEM;
			goto out;
		}

		pipe = new_pipe;
		pipe->allocated = true;
	}

	ret = __media_pipeline_start(pad, pipe);
	if (ret)
		kfree(new_pipe);

out:
	mutex_unlock(&mdev->graph_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(media_pipeline_alloc_start);

/* -----------------------------------------------------------------------------
 * Links management
 */

static struct media_link *media_add_link(struct list_head *head)
{
	struct media_link *link;

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (link == NULL)
		return NULL;

	list_add_tail(&link->list, head);

	return link;
}

static void __media_entity_remove_link(struct media_entity *entity,
				       struct media_link *link)
{
	struct media_link *rlink, *tmp;
	struct media_entity *remote;

	/* Remove the reverse links for a data link. */
	if ((link->flags & MEDIA_LNK_FL_LINK_TYPE) == MEDIA_LNK_FL_DATA_LINK) {
		if (link->source->entity == entity)
			remote = link->sink->entity;
		else
			remote = link->source->entity;

		list_for_each_entry_safe(rlink, tmp, &remote->links, list) {
			if (rlink != link->reverse)
				continue;

			if (link->source->entity == entity)
				remote->num_backlinks--;

			/* Remove the remote link */
			list_del(&rlink->list);
			media_gobj_destroy(&rlink->graph_obj);
			kfree(rlink);

			if (--remote->num_links == 0)
				break;
		}
	}

	list_del(&link->list);
	media_gobj_destroy(&link->graph_obj);
	kfree(link);
}

int media_get_pad_index(struct media_entity *entity, bool is_sink,
			enum media_pad_signal_type sig_type)
{
	int i;
	bool pad_is_sink;

	if (!entity)
		return -EINVAL;

	for (i = 0; i < entity->num_pads; i++) {
		if (entity->pads[i].flags & MEDIA_PAD_FL_SINK)
			pad_is_sink = true;
		else if (entity->pads[i].flags & MEDIA_PAD_FL_SOURCE)
			pad_is_sink = false;
		else
			continue;	/* This is an error! */

		if (pad_is_sink != is_sink)
			continue;
		if (entity->pads[i].sig_type == sig_type)
			return i;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(media_get_pad_index);

int
media_create_pad_link(struct media_entity *source, u16 source_pad,
			 struct media_entity *sink, u16 sink_pad, u32 flags)
{
	struct media_link *link;
	struct media_link *backlink;

	if (WARN_ON(!source || !sink) ||
	    WARN_ON(source_pad >= source->num_pads) ||
	    WARN_ON(sink_pad >= sink->num_pads))
		return -EINVAL;
	if (WARN_ON(!(source->pads[source_pad].flags & MEDIA_PAD_FL_SOURCE)))
		return -EINVAL;
	if (WARN_ON(!(sink->pads[sink_pad].flags & MEDIA_PAD_FL_SINK)))
		return -EINVAL;

	link = media_add_link(&source->links);
	if (link == NULL)
		return -ENOMEM;

	link->source = &source->pads[source_pad];
	link->sink = &sink->pads[sink_pad];
	link->flags = flags & ~MEDIA_LNK_FL_INTERFACE_LINK;

	/* Initialize graph object embedded at the new link */
	media_gobj_create(source->graph_obj.mdev, MEDIA_GRAPH_LINK,
			&link->graph_obj);

	/* Create the backlink. Backlinks are used to help graph traversal and
	 * are not reported to userspace.
	 */
	backlink = media_add_link(&sink->links);
	if (backlink == NULL) {
		__media_entity_remove_link(source, link);
		return -ENOMEM;
	}

	backlink->source = &source->pads[source_pad];
	backlink->sink = &sink->pads[sink_pad];
	backlink->flags = flags;
	backlink->is_backlink = true;

	/* Initialize graph object embedded at the new link */
	media_gobj_create(sink->graph_obj.mdev, MEDIA_GRAPH_LINK,
			&backlink->graph_obj);

	link->reverse = backlink;
	backlink->reverse = link;

	sink->num_backlinks++;
	sink->num_links++;
	source->num_links++;

	return 0;
}
EXPORT_SYMBOL_GPL(media_create_pad_link);

int media_create_pad_links(const struct media_device *mdev,
			   const u32 source_function,
			   struct media_entity *source,
			   const u16 source_pad,
			   const u32 sink_function,
			   struct media_entity *sink,
			   const u16 sink_pad,
			   u32 flags,
			   const bool allow_both_undefined)
{
	struct media_entity *entity;
	unsigned function;
	int ret;

	/* Trivial case: 1:1 relation */
	if (source && sink)
		return media_create_pad_link(source, source_pad,
					     sink, sink_pad, flags);

	/* Worse case scenario: n:n relation */
	if (!source && !sink) {
		if (!allow_both_undefined)
			return 0;
		media_device_for_each_entity(source, mdev) {
			if (source->function != source_function)
				continue;
			media_device_for_each_entity(sink, mdev) {
				if (sink->function != sink_function)
					continue;
				ret = media_create_pad_link(source, source_pad,
							    sink, sink_pad,
							    flags);
				if (ret)
					return ret;
				flags &= ~(MEDIA_LNK_FL_ENABLED |
					   MEDIA_LNK_FL_IMMUTABLE);
			}
		}
		return 0;
	}

	/* Handle 1:n and n:1 cases */
	if (source)
		function = sink_function;
	else
		function = source_function;

	media_device_for_each_entity(entity, mdev) {
		if (entity->function != function)
			continue;

		if (source)
			ret = media_create_pad_link(source, source_pad,
						    entity, sink_pad, flags);
		else
			ret = media_create_pad_link(entity, source_pad,
						    sink, sink_pad, flags);
		if (ret)
			return ret;
		flags &= ~(MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(media_create_pad_links);

void __media_entity_remove_links(struct media_entity *entity)
{
	struct media_link *link, *tmp;

	list_for_each_entry_safe(link, tmp, &entity->links, list)
		__media_entity_remove_link(entity, link);

	entity->num_links = 0;
	entity->num_backlinks = 0;
}
EXPORT_SYMBOL_GPL(__media_entity_remove_links);

void media_entity_remove_links(struct media_entity *entity)
{
	struct media_device *mdev = entity->graph_obj.mdev;

	/* Do nothing if the entity is not registered. */
	if (mdev == NULL)
		return;

	mutex_lock(&mdev->graph_mutex);
	__media_entity_remove_links(entity);
	mutex_unlock(&mdev->graph_mutex);
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

int __media_entity_setup_link(struct media_link *link, u32 flags)
{
	const u32 mask = MEDIA_LNK_FL_ENABLED;
	struct media_device *mdev;
	struct media_pad *source, *sink;
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

	source = link->source;
	sink = link->sink;

	if (!(link->flags & MEDIA_LNK_FL_DYNAMIC) &&
	    (media_pad_is_streaming(source) || media_pad_is_streaming(sink)))
		return -EBUSY;

	mdev = source->graph_obj.mdev;

	if (mdev->ops && mdev->ops->link_notify) {
		ret = mdev->ops->link_notify(link, flags,
					     MEDIA_DEV_NOTIFY_PRE_LINK_CH);
		if (ret < 0)
			return ret;
	}

	ret = __media_entity_setup_link_notify(link, flags);

	if (mdev->ops && mdev->ops->link_notify)
		mdev->ops->link_notify(link, flags,
				       MEDIA_DEV_NOTIFY_POST_LINK_CH);

	return ret;
}
EXPORT_SYMBOL_GPL(__media_entity_setup_link);

int media_entity_setup_link(struct media_link *link, u32 flags)
{
	int ret;

	mutex_lock(&link->graph_obj.mdev->graph_mutex);
	ret = __media_entity_setup_link(link, flags);
	mutex_unlock(&link->graph_obj.mdev->graph_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(media_entity_setup_link);

struct media_link *
media_entity_find_link(struct media_pad *source, struct media_pad *sink)
{
	struct media_link *link;

	for_each_media_entity_data_link(source->entity, link) {
		if (link->source->entity == source->entity &&
		    link->source->index == source->index &&
		    link->sink->entity == sink->entity &&
		    link->sink->index == sink->index)
			return link;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(media_entity_find_link);

struct media_pad *media_pad_remote_pad_first(const struct media_pad *pad)
{
	struct media_link *link;

	for_each_media_entity_data_link(pad->entity, link) {
		if (!(link->flags & MEDIA_LNK_FL_ENABLED))
			continue;

		if (link->source == pad)
			return link->sink;

		if (link->sink == pad)
			return link->source;
	}

	return NULL;

}
EXPORT_SYMBOL_GPL(media_pad_remote_pad_first);

struct media_pad *
media_entity_remote_pad_unique(const struct media_entity *entity,
			       unsigned int type)
{
	struct media_pad *pad = NULL;
	struct media_link *link;

	list_for_each_entry(link, &entity->links, list) {
		struct media_pad *local_pad;
		struct media_pad *remote_pad;

		if (((link->flags & MEDIA_LNK_FL_LINK_TYPE) !=
		     MEDIA_LNK_FL_DATA_LINK) ||
		    !(link->flags & MEDIA_LNK_FL_ENABLED))
			continue;

		if (type == MEDIA_PAD_FL_SOURCE) {
			local_pad = link->sink;
			remote_pad = link->source;
		} else {
			local_pad = link->source;
			remote_pad = link->sink;
		}

		if (local_pad->entity == entity) {
			if (pad)
				return ERR_PTR(-ENOTUNIQ);

			pad = remote_pad;
		}
	}

	if (!pad)
		return ERR_PTR(-ENOLINK);

	return pad;
}
EXPORT_SYMBOL_GPL(media_entity_remote_pad_unique);

struct media_pad *media_pad_remote_pad_unique(const struct media_pad *pad)
{
	struct media_pad *found_pad = NULL;
	struct media_link *link;

	list_for_each_entry(link, &pad->entity->links, list) {
		struct media_pad *remote_pad;

		if (!(link->flags & MEDIA_LNK_FL_ENABLED))
			continue;

		if (link->sink == pad)
			remote_pad = link->source;
		else if (link->source == pad)
			remote_pad = link->sink;
		else
			continue;

		if (found_pad)
			return ERR_PTR(-ENOTUNIQ);

		found_pad = remote_pad;
	}

	if (!found_pad)
		return ERR_PTR(-ENOLINK);

	return found_pad;
}
EXPORT_SYMBOL_GPL(media_pad_remote_pad_unique);

int media_entity_get_fwnode_pad(struct media_entity *entity,
				struct fwnode_handle *fwnode,
				unsigned long direction_flags)
{
	struct fwnode_endpoint endpoint;
	unsigned int i;
	int ret;

	if (!entity->ops || !entity->ops->get_fwnode_pad) {
		for (i = 0; i < entity->num_pads; i++) {
			if (entity->pads[i].flags & direction_flags)
				return i;
		}

		return -ENXIO;
	}

	ret = fwnode_graph_parse_endpoint(fwnode, &endpoint);
	if (ret)
		return ret;

	ret = entity->ops->get_fwnode_pad(entity, &endpoint);
	if (ret < 0)
		return ret;

	if (ret >= entity->num_pads)
		return -ENXIO;

	if (!(entity->pads[ret].flags & direction_flags))
		return -ENXIO;

	return ret;
}
EXPORT_SYMBOL_GPL(media_entity_get_fwnode_pad);

struct media_pipeline *media_entity_pipeline(struct media_entity *entity)
{
	struct media_pad *pad;

	media_entity_for_each_pad(entity, pad) {
		if (pad->pipe)
			return pad->pipe;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(media_entity_pipeline);

struct media_pipeline *media_pad_pipeline(struct media_pad *pad)
{
	return pad->pipe;
}
EXPORT_SYMBOL_GPL(media_pad_pipeline);

static void media_interface_init(struct media_device *mdev,
				 struct media_interface *intf,
				 u32 gobj_type,
				 u32 intf_type, u32 flags)
{
	intf->type = intf_type;
	intf->flags = flags;
	INIT_LIST_HEAD(&intf->links);

	media_gobj_create(mdev, gobj_type, &intf->graph_obj);
}

/* Functions related to the media interface via device nodes */

struct media_intf_devnode *media_devnode_create(struct media_device *mdev,
						u32 type, u32 flags,
						u32 major, u32 minor)
{
	struct media_intf_devnode *devnode;

	devnode = kzalloc(sizeof(*devnode), GFP_KERNEL);
	if (!devnode)
		return NULL;

	devnode->major = major;
	devnode->minor = minor;

	media_interface_init(mdev, &devnode->intf, MEDIA_GRAPH_INTF_DEVNODE,
			     type, flags);

	return devnode;
}
EXPORT_SYMBOL_GPL(media_devnode_create);

void media_devnode_remove(struct media_intf_devnode *devnode)
{
	media_remove_intf_links(&devnode->intf);
	media_gobj_destroy(&devnode->intf.graph_obj);
	kfree(devnode);
}
EXPORT_SYMBOL_GPL(media_devnode_remove);

struct media_link *media_create_intf_link(struct media_entity *entity,
					    struct media_interface *intf,
					    u32 flags)
{
	struct media_link *link;

	link = media_add_link(&intf->links);
	if (link == NULL)
		return NULL;

	link->intf = intf;
	link->entity = entity;
	link->flags = flags | MEDIA_LNK_FL_INTERFACE_LINK;

	/* Initialize graph object embedded at the new link */
	media_gobj_create(intf->graph_obj.mdev, MEDIA_GRAPH_LINK,
			&link->graph_obj);

	return link;
}
EXPORT_SYMBOL_GPL(media_create_intf_link);

void __media_remove_intf_link(struct media_link *link)
{
	list_del(&link->list);
	media_gobj_destroy(&link->graph_obj);
	kfree(link);
}
EXPORT_SYMBOL_GPL(__media_remove_intf_link);

void media_remove_intf_link(struct media_link *link)
{
	struct media_device *mdev = link->graph_obj.mdev;

	/* Do nothing if the intf is not registered. */
	if (mdev == NULL)
		return;

	mutex_lock(&mdev->graph_mutex);
	__media_remove_intf_link(link);
	mutex_unlock(&mdev->graph_mutex);
}
EXPORT_SYMBOL_GPL(media_remove_intf_link);

void __media_remove_intf_links(struct media_interface *intf)
{
	struct media_link *link, *tmp;

	list_for_each_entry_safe(link, tmp, &intf->links, list)
		__media_remove_intf_link(link);

}
EXPORT_SYMBOL_GPL(__media_remove_intf_links);

void media_remove_intf_links(struct media_interface *intf)
{
	struct media_device *mdev = intf->graph_obj.mdev;

	/* Do nothing if the intf is not registered. */
	if (mdev == NULL)
		return;

	mutex_lock(&mdev->graph_mutex);
	__media_remove_intf_links(intf);
	mutex_unlock(&mdev->graph_mutex);
}
EXPORT_SYMBOL_GPL(media_remove_intf_links);

struct media_link *media_create_ancillary_link(struct media_entity *primary,
					       struct media_entity *ancillary)
{
	struct media_link *link;

	link = media_add_link(&primary->links);
	if (!link)
		return ERR_PTR(-ENOMEM);

	link->gobj0 = &primary->graph_obj;
	link->gobj1 = &ancillary->graph_obj;
	link->flags = MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED |
		      MEDIA_LNK_FL_ANCILLARY_LINK;

	/* Initialize graph object embedded in the new link */
	media_gobj_create(primary->graph_obj.mdev, MEDIA_GRAPH_LINK,
			  &link->graph_obj);

	return link;
}
EXPORT_SYMBOL_GPL(media_create_ancillary_link);

struct media_link *__media_entity_next_link(struct media_entity *entity,
					    struct media_link *link,
					    unsigned long link_type)
{
	link = link ? list_next_entry(link, list)
		    : list_first_entry(&entity->links, typeof(*link), list);

	list_for_each_entry_from(link, &entity->links, list)
		if ((link->flags & MEDIA_LNK_FL_LINK_TYPE) == link_type)
			return link;

	return NULL;
}
EXPORT_SYMBOL_GPL(__media_entity_next_link);
