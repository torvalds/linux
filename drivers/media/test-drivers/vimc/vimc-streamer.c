// SPDX-License-Identifier: GPL-2.0+
/*
 * vimc-streamer.c Virtual Media Controller Driver
 *
 * Copyright (C) 2018 Lucas A. M. Magalhães <lucmaga@gmail.com>
 *
 */

#include <linux/init.h>
#include <linux/freezer.h>
#include <linux/kthread.h>

#include "vimc-streamer.h"

/**
 * vimc_get_source_entity - get the entity connected with the first sink pad
 *
 * @ent:	reference media_entity
 *
 * Helper function that returns the media entity containing the source pad
 * linked with the first sink pad from the given media entity pad list.
 *
 * Return: The source pad or NULL, if it wasn't found.
 */
static struct media_entity *vimc_get_source_entity(struct media_entity *ent)
{
	struct media_pad *pad;
	int i;

	for (i = 0; i < ent->num_pads; i++) {
		if (ent->pads[i].flags & MEDIA_PAD_FL_SOURCE)
			continue;
		pad = media_pad_remote_pad_first(&ent->pads[i]);
		return pad ? pad->entity : NULL;
	}
	return NULL;
}

/**
 * vimc_streamer_pipeline_terminate - Disable stream in all ved in stream
 *
 * @stream: the pointer to the stream structure with the pipeline to be
 *	    disabled.
 *
 * Calls s_stream to disable the stream in each entity of the pipeline
 *
 */
static void vimc_streamer_pipeline_terminate(struct vimc_stream *stream)
{
	struct vimc_ent_device *ved;
	struct v4l2_subdev *sd;

	while (stream->pipe_size) {
		stream->pipe_size--;
		ved = stream->ved_pipeline[stream->pipe_size];
		stream->ved_pipeline[stream->pipe_size] = NULL;

		if (!is_media_entity_v4l2_subdev(ved->ent))
			continue;

		sd = media_entity_to_v4l2_subdev(ved->ent);
		v4l2_subdev_call(sd, video, s_stream, 0);
	}
}

/**
 * vimc_streamer_pipeline_init - Initializes the stream structure
 *
 * @stream: the pointer to the stream structure to be initialized
 * @ved:    the pointer to the vimc entity initializing the stream
 *
 * Initializes the stream structure. Walks through the entity graph to
 * construct the pipeline used later on the streamer thread.
 * Calls vimc_streamer_s_stream() to enable stream in all entities of
 * the pipeline.
 *
 * Return: 0 if success, error code otherwise.
 */
static int vimc_streamer_pipeline_init(struct vimc_stream *stream,
				       struct vimc_ent_device *ved)
{
	struct media_entity *entity;
	struct video_device *vdev;
	struct v4l2_subdev *sd;
	int ret = 0;

	stream->pipe_size = 0;
	while (stream->pipe_size < VIMC_STREAMER_PIPELINE_MAX_SIZE) {
		if (!ved) {
			vimc_streamer_pipeline_terminate(stream);
			return -EINVAL;
		}
		stream->ved_pipeline[stream->pipe_size++] = ved;

		if (is_media_entity_v4l2_subdev(ved->ent)) {
			sd = media_entity_to_v4l2_subdev(ved->ent);
			ret = v4l2_subdev_call(sd, video, s_stream, 1);
			if (ret && ret != -ENOIOCTLCMD) {
				dev_err(ved->dev, "subdev_call error %s\n",
					ved->ent->name);
				vimc_streamer_pipeline_terminate(stream);
				return ret;
			}
		}

		entity = vimc_get_source_entity(ved->ent);
		/* Check if the end of the pipeline was reached */
		if (!entity) {
			/* the first entity of the pipe should be source only */
			if (!vimc_is_source(ved->ent)) {
				dev_err(ved->dev,
					"first entity in the pipe '%s' is not a source\n",
					ved->ent->name);
				vimc_streamer_pipeline_terminate(stream);
				return -EPIPE;
			}
			return 0;
		}

		/* Get the next device in the pipeline */
		if (is_media_entity_v4l2_subdev(entity)) {
			sd = media_entity_to_v4l2_subdev(entity);
			ved = v4l2_get_subdevdata(sd);
		} else {
			vdev = container_of(entity,
					    struct video_device,
					    entity);
			ved = video_get_drvdata(vdev);
		}
	}

	vimc_streamer_pipeline_terminate(stream);
	return -EINVAL;
}

/**
 * vimc_streamer_thread - Process frames through the pipeline
 *
 * @data:	vimc_stream struct of the current stream
 *
 * From the source to the sink, gets a frame from each subdevice and send to
 * the next one of the pipeline at a fixed framerate.
 *
 * Return:
 * Always zero (created as ``int`` instead of ``void`` to comply with
 * kthread API).
 */
static int vimc_streamer_thread(void *data)
{
	struct vimc_stream *stream = data;
	u8 *frame = NULL;
	int i;

	set_freezable();

	for (;;) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		for (i = stream->pipe_size - 1; i >= 0; i--) {
			frame = stream->ved_pipeline[i]->process_frame(
					stream->ved_pipeline[i], frame);
			if (!frame || IS_ERR(frame))
				break;
		}
		//wait for 60hz
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 60);
	}

	return 0;
}

/**
 * vimc_streamer_s_stream - Start/stop the streaming on the media pipeline
 *
 * @stream:	the pointer to the stream structure of the current stream
 * @ved:	pointer to the vimc entity of the entity of the stream
 * @enable:	flag to determine if stream should start/stop
 *
 * When starting, check if there is no ``stream->kthread`` allocated. This
 * should indicate that a stream is already running. Then, it initializes the
 * pipeline, creates and runs a kthread to consume buffers through the pipeline.
 * When stopping, analogously check if there is a stream running, stop the
 * thread and terminates the pipeline.
 *
 * Return: 0 if success, error code otherwise.
 */
int vimc_streamer_s_stream(struct vimc_stream *stream,
			   struct vimc_ent_device *ved,
			   int enable)
{
	int ret;

	if (!stream || !ved)
		return -EINVAL;

	if (enable) {
		if (stream->kthread)
			return 0;

		ret = vimc_streamer_pipeline_init(stream, ved);
		if (ret)
			return ret;

		stream->kthread = kthread_run(vimc_streamer_thread, stream,
					      "vimc-streamer thread");

		if (IS_ERR(stream->kthread)) {
			ret = PTR_ERR(stream->kthread);
			dev_err(ved->dev, "kthread_run failed with %d\n", ret);
			vimc_streamer_pipeline_terminate(stream);
			stream->kthread = NULL;
			return ret;
		}

	} else {
		if (!stream->kthread)
			return 0;

		ret = kthread_stop(stream->kthread);
		/*
		 * kthread_stop returns -EINTR in cases when streamon was
		 * immediately followed by streamoff, and the thread didn't had
		 * a chance to run. Ignore errors to stop the stream in the
		 * pipeline.
		 */
		if (ret)
			dev_dbg(ved->dev, "kthread_stop returned '%d'\n", ret);

		stream->kthread = NULL;

		vimc_streamer_pipeline_terminate(stream);
	}

	return 0;
}
