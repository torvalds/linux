// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Chrome OS EC Sensor hub FIFO.
 *
 * Copyright 2020 Google LLC
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_sensorhub.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/slab.h>

/* Precision of fixed point for the m values from the filter */
#define M_PRECISION BIT(23)

/* Only activate the filter once we have at least this many elements. */
#define TS_HISTORY_THRESHOLD 8

/*
 * If we don't have any history entries for this long, empty the filter to
 * make sure there are no big discontinuities.
 */
#define TS_HISTORY_BORED_US 500000

/* To measure by how much the filter is overshooting, if it happens. */
#define FUTURE_TS_ANALYTICS_COUNT_MAX 100

static inline int
cros_sensorhub_send_sample(struct cros_ec_sensorhub *sensorhub,
			   struct cros_ec_sensors_ring_sample *sample)
{
	cros_ec_sensorhub_push_data_cb_t cb;
	int id = sample->sensor_id;
	struct iio_dev *indio_dev;

	if (id >= sensorhub->sensor_num)
		return -EINVAL;

	cb = sensorhub->push_data[id].push_data_cb;
	if (!cb)
		return 0;

	indio_dev = sensorhub->push_data[id].indio_dev;

	if (sample->flag & MOTIONSENSE_SENSOR_FLAG_FLUSH)
		return 0;

	return cb(indio_dev, sample->vector, sample->timestamp);
}

/**
 * cros_ec_sensorhub_register_push_data() - register the callback to the hub.
 *
 * @sensorhub : Sensor Hub object
 * @sensor_num : The sensor the caller is interested in.
 * @indio_dev : The iio device to use when a sample arrives.
 * @cb : The callback to call when a sample arrives.
 *
 * The callback cb will be used by cros_ec_sensorhub_ring to distribute events
 * from the EC.
 *
 * Return: 0 when callback is registered.
 *         EINVAL is the sensor number is invalid or the slot already used.
 */
int cros_ec_sensorhub_register_push_data(struct cros_ec_sensorhub *sensorhub,
					 u8 sensor_num,
					 struct iio_dev *indio_dev,
					 cros_ec_sensorhub_push_data_cb_t cb)
{
	if (sensor_num >= sensorhub->sensor_num)
		return -EINVAL;
	if (sensorhub->push_data[sensor_num].indio_dev)
		return -EINVAL;

	sensorhub->push_data[sensor_num].indio_dev = indio_dev;
	sensorhub->push_data[sensor_num].push_data_cb = cb;

	return 0;
}
EXPORT_SYMBOL_GPL(cros_ec_sensorhub_register_push_data);

void cros_ec_sensorhub_unregister_push_data(struct cros_ec_sensorhub *sensorhub,
					    u8 sensor_num)
{
	sensorhub->push_data[sensor_num].indio_dev = NULL;
	sensorhub->push_data[sensor_num].push_data_cb = NULL;
}
EXPORT_SYMBOL_GPL(cros_ec_sensorhub_unregister_push_data);

/**
 * cros_ec_sensorhub_ring_fifo_enable() - Enable or disable interrupt generation
 *					  for FIFO events.
 * @sensorhub: Sensor Hub object
 * @on: true when events are requested.
 *
 * To be called before sleeping or when noone is listening.
 * Return: 0 on success, or an error when we can not communicate with the EC.
 *
 */
int cros_ec_sensorhub_ring_fifo_enable(struct cros_ec_sensorhub *sensorhub,
				       bool on)
{
	int ret, i;

	mutex_lock(&sensorhub->cmd_lock);
	if (sensorhub->tight_timestamps)
		for (i = 0; i < sensorhub->sensor_num; i++)
			sensorhub->batch_state[i].last_len = 0;

	sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_INT_ENABLE;
	sensorhub->params->fifo_int_enable.enable = on;

	sensorhub->msg->outsize = sizeof(struct ec_params_motion_sense);
	sensorhub->msg->insize = sizeof(struct ec_response_motion_sense);

	ret = cros_ec_cmd_xfer_status(sensorhub->ec->ec_dev, sensorhub->msg);
	mutex_unlock(&sensorhub->cmd_lock);

	/* We expect to receive a payload of 4 bytes, ignore. */
	if (ret > 0)
		ret = 0;

	return ret;
}

static int cros_ec_sensor_ring_median_cmp(const void *pv1, const void *pv2)
{
	s64 v1 = *(s64 *)pv1;
	s64 v2 = *(s64 *)pv2;

	if (v1 > v2)
		return 1;
	else if (v1 < v2)
		return -1;
	else
		return 0;
}

/*
 * cros_ec_sensor_ring_median: Gets median of an array of numbers
 *
 * For now it's implemented using an inefficient > O(n) sort then return
 * the middle element. A more optimal method would be something like
 * quickselect, but given that n = 64 we can probably live with it in the
 * name of clarity.
 *
 * Warning: the input array gets modified (sorted)!
 */
static s64 cros_ec_sensor_ring_median(s64 *array, size_t length)
{
	sort(array, length, sizeof(s64), cros_ec_sensor_ring_median_cmp, NULL);
	return array[length / 2];
}

/*
 * IRQ Timestamp Filtering
 *
 * Lower down in cros_ec_sensor_ring_process_event(), for each sensor event
 * we have to calculate it's timestamp in the AP timebase. There are 3 time
 * points:
 *   a - EC timebase, sensor event
 *   b - EC timebase, IRQ
 *   c - AP timebase, IRQ
 *   a' - what we want: sensor even in AP timebase
 *
 * While a and b are recorded at accurate times (due to the EC real time
 * nature); c is pretty untrustworthy, even though it's recorded the
 * first thing in ec_irq_handler(). There is a very good change we'll get
 * added lantency due to:
 *   other irqs
 *   ddrfreq
 *   cpuidle
 *
 * Normally a' = c - b + a, but if we do that naive math any jitter in c
 * will get coupled in a', which we don't want. We want a function
 * a' = cros_ec_sensor_ring_ts_filter(a) which will filter out outliers in c.
 *
 * Think of a graph of AP time(b) on the y axis vs EC time(c) on the x axis.
 * The slope of the line won't be exactly 1, there will be some clock drift
 * between the 2 chips for various reasons (mechanical stress, temperature,
 * voltage). We need to extrapolate values for a future x, without trusting
 * recent y values too much.
 *
 * We use a median filter for the slope, then another median filter for the
 * y-intercept to calculate this function:
 *   dx[n] = x[n-1] - x[n]
 *   dy[n] = x[n-1] - x[n]
 *   m[n] = dy[n] / dx[n]
 *   median_m = median(m[n-k:n])
 *   error[i] = y[n-i] - median_m * x[n-i]
 *   median_error = median(error[:k])
 *   predicted_y = median_m * x + median_error
 *
 * Implementation differences from above:
 * - Redefined y to be actually c - b, this gives us a lot more precision
 * to do the math. (c-b)/b variations are more obvious than c/b variations.
 * - Since we don't have floating point, any operations involving slope are
 * done using fixed point math (*M_PRECISION)
 * - Since x and y grow with time, we keep zeroing the graph (relative to
 * the last sample), this way math involving *x[n-i] will not overflow
 * - EC timestamps are kept in us, it improves the slope calculation precision
 */

/**
 * cros_ec_sensor_ring_ts_filter_update() - Update filter history.
 *
 * @state: Filter information.
 * @b: IRQ timestamp, EC timebase (us)
 * @c: IRQ timestamp, AP timebase (ns)
 *
 * Given a new IRQ timestamp pair (EC and AP timebases), add it to the filter
 * history.
 */
static void
cros_ec_sensor_ring_ts_filter_update(struct cros_ec_sensors_ts_filter_state
				     *state,
				     s64 b, s64 c)
{
	s64 x, y;
	s64 dx, dy;
	s64 m; /* stored as *M_PRECISION */
	s64 *m_history_copy = state->temp_buf;
	s64 *error = state->temp_buf;
	int i;

	/* we trust b the most, that'll be our independent variable */
	x = b;
	/* y is the offset between AP and EC times, in ns */
	y = c - b * 1000;

	dx = (state->x_history[0] + state->x_offset) - x;
	if (dx == 0)
		return; /* we already have this irq in the history */
	dy = (state->y_history[0] + state->y_offset) - y;
	m = div64_s64(dy * M_PRECISION, dx);

	/* Empty filter if we haven't seen any action in a while. */
	if (-dx > TS_HISTORY_BORED_US)
		state->history_len = 0;

	/* Move everything over, also update offset to all absolute coords .*/
	for (i = state->history_len - 1; i >= 1; i--) {
		state->x_history[i] = state->x_history[i - 1] + dx;
		state->y_history[i] = state->y_history[i - 1] + dy;

		state->m_history[i] = state->m_history[i - 1];
		/*
		 * Also use the same loop to copy m_history for future
		 * median extraction.
		 */
		m_history_copy[i] = state->m_history[i - 1];
	}

	/* Store the x and y, but remember offset is actually last sample. */
	state->x_offset = x;
	state->y_offset = y;
	state->x_history[0] = 0;
	state->y_history[0] = 0;

	state->m_history[0] = m;
	m_history_copy[0] = m;

	if (state->history_len < CROS_EC_SENSORHUB_TS_HISTORY_SIZE)
		state->history_len++;

	/* Precalculate things for the filter. */
	if (state->history_len > TS_HISTORY_THRESHOLD) {
		state->median_m =
		    cros_ec_sensor_ring_median(m_history_copy,
					       state->history_len - 1);

		/*
		 * Calculate y-intercepts as if m_median is the slope and
		 * points in the history are on the line. median_error will
		 * still be in the offset coordinate system.
		 */
		for (i = 0; i < state->history_len; i++)
			error[i] = state->y_history[i] -
				div_s64(state->median_m * state->x_history[i],
					M_PRECISION);
		state->median_error =
			cros_ec_sensor_ring_median(error, state->history_len);
	} else {
		state->median_m = 0;
		state->median_error = 0;
	}
}

/**
 * cros_ec_sensor_ring_ts_filter() - Translate EC timebase timestamp to AP
 *                                   timebase
 *
 * @state: filter information.
 * @x: any ec timestamp (us):
 *
 * cros_ec_sensor_ring_ts_filter(a) => a' event timestamp, AP timebase
 * cros_ec_sensor_ring_ts_filter(b) => calculated timestamp when the EC IRQ
 *                           should have happened on the AP, with low jitter
 *
 * Note: The filter will only activate once state->history_len goes
 * over TS_HISTORY_THRESHOLD. Otherwise it'll just do the naive c - b + a
 * transform.
 *
 * How to derive the formula, starting from:
 *   f(x) = median_m * x + median_error
 * That's the calculated AP - EC offset (at the x point in time)
 * Undo the coordinate system transform:
 *   f(x) = median_m * (x - x_offset) + median_error + y_offset
 * Remember to undo the "y = c - b * 1000" modification:
 *   f(x) = median_m * (x - x_offset) + median_error + y_offset + x * 1000
 *
 * Return: timestamp in AP timebase (ns)
 */
static s64
cros_ec_sensor_ring_ts_filter(struct cros_ec_sensors_ts_filter_state *state,
			      s64 x)
{
	return div_s64(state->median_m * (x - state->x_offset), M_PRECISION)
	       + state->median_error + state->y_offset + x * 1000;
}

/*
 * Since a and b were originally 32 bit values from the EC,
 * they overflow relatively often, casting is not enough, so we need to
 * add an offset.
 */
static void
cros_ec_sensor_ring_fix_overflow(s64 *ts,
				 const s64 overflow_period,
				 struct cros_ec_sensors_ec_overflow_state
				 *state)
{
	s64 adjust;

	*ts += state->offset;
	if (abs(state->last - *ts) > (overflow_period / 2)) {
		adjust = state->last > *ts ? overflow_period : -overflow_period;
		state->offset += adjust;
		*ts += adjust;
	}
	state->last = *ts;
}

static void
cros_ec_sensor_ring_check_for_past_timestamp(struct cros_ec_sensorhub
					     *sensorhub,
					     struct cros_ec_sensors_ring_sample
					     *sample)
{
	const u8 sensor_id = sample->sensor_id;

	/* If this event is earlier than one we saw before... */
	if (sensorhub->batch_state[sensor_id].newest_sensor_event >
	    sample->timestamp)
		/* mark it for spreading. */
		sample->timestamp =
			sensorhub->batch_state[sensor_id].last_ts;
	else
		sensorhub->batch_state[sensor_id].newest_sensor_event =
			sample->timestamp;
}

/**
 * cros_ec_sensor_ring_process_event() - Process one EC FIFO event
 *
 * @sensorhub: Sensor Hub object.
 * @fifo_info: FIFO information from the EC (includes b point, EC timebase).
 * @fifo_timestamp: EC IRQ, kernel timebase (aka c).
 * @current_timestamp: calculated event timestamp, kernel timebase (aka a').
 * @in: incoming FIFO event from EC (includes a point, EC timebase).
 * @out: outgoing event to user space (includes a').
 *
 * Process one EC event, add it in the ring if necessary.
 *
 * Return: true if out event has been populated.
 */
static bool
cros_ec_sensor_ring_process_event(struct cros_ec_sensorhub *sensorhub,
				const struct ec_response_motion_sense_fifo_info
				*fifo_info,
				const ktime_t fifo_timestamp,
				ktime_t *current_timestamp,
				struct ec_response_motion_sensor_data *in,
				struct cros_ec_sensors_ring_sample *out)
{
	const s64 now = cros_ec_get_time_ns();
	int axis, async_flags;

	/* Do not populate the filter based on asynchronous events. */
	async_flags = in->flags &
		(MOTIONSENSE_SENSOR_FLAG_ODR | MOTIONSENSE_SENSOR_FLAG_FLUSH);

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP && !async_flags) {
		s64 a = in->timestamp;
		s64 b = fifo_info->timestamp;
		s64 c = fifo_timestamp;

		cros_ec_sensor_ring_fix_overflow(&a, 1LL << 32,
					  &sensorhub->overflow_a);
		cros_ec_sensor_ring_fix_overflow(&b, 1LL << 32,
					  &sensorhub->overflow_b);

		if (sensorhub->tight_timestamps) {
			cros_ec_sensor_ring_ts_filter_update(
					&sensorhub->filter, b, c);
			*current_timestamp = cros_ec_sensor_ring_ts_filter(
					&sensorhub->filter, a);
		} else {
			s64 new_timestamp;

			/*
			 * Disable filtering since we might add more jitter
			 * if b is in a random point in time.
			 */
			new_timestamp = c - b * 1000 + a * 1000;
			/*
			 * The timestamp can be stale if we had to use the fifo
			 * info timestamp.
			 */
			if (new_timestamp - *current_timestamp > 0)
				*current_timestamp = new_timestamp;
		}
	}

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_ODR) {
		if (sensorhub->tight_timestamps) {
			sensorhub->batch_state[in->sensor_num].last_len = 0;
			sensorhub->batch_state[in->sensor_num].penul_len = 0;
		}
		/*
		 * ODR change is only useful for the sensor_ring, it does not
		 * convey information to clients.
		 */
		return false;
	}

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_FLUSH) {
		out->sensor_id = in->sensor_num;
		out->timestamp = *current_timestamp;
		out->flag = in->flags;
		if (sensorhub->tight_timestamps)
			sensorhub->batch_state[out->sensor_id].last_len = 0;
		/*
		 * No other payload information provided with
		 * flush ack.
		 */
		return true;
	}

	if (in->flags & MOTIONSENSE_SENSOR_FLAG_TIMESTAMP)
		/* If we just have a timestamp, skip this entry. */
		return false;

	/* Regular sample */
	out->sensor_id = in->sensor_num;
	if (*current_timestamp - now > 0) {
		/*
		 * This fix is needed to overcome the timestamp filter putting
		 * events in the future.
		 */
		sensorhub->future_timestamp_total_ns +=
			*current_timestamp - now;
		if (++sensorhub->future_timestamp_count ==
				FUTURE_TS_ANALYTICS_COUNT_MAX) {
			s64 avg = div_s64(sensorhub->future_timestamp_total_ns,
					sensorhub->future_timestamp_count);
			dev_warn_ratelimited(sensorhub->dev,
					     "100 timestamps in the future, %lldns shaved on average\n",
					     avg);
			sensorhub->future_timestamp_count = 0;
			sensorhub->future_timestamp_total_ns = 0;
		}
		out->timestamp = now;
	} else {
		out->timestamp = *current_timestamp;
	}

	out->flag = in->flags;
	for (axis = 0; axis < 3; axis++)
		out->vector[axis] = in->data[axis];

	if (sensorhub->tight_timestamps)
		cros_ec_sensor_ring_check_for_past_timestamp(sensorhub, out);
	return true;
}

/*
 * cros_ec_sensor_ring_spread_add: Calculate proper timestamps then add to
 *                                 ringbuffer.
 *
 * This is the new spreading code, assumes every sample's timestamp
 * preceeds the sample. Run if tight_timestamps == true.
 *
 * Sometimes the EC receives only one interrupt (hence timestamp) for
 * a batch of samples. Only the first sample will have the correct
 * timestamp. So we must interpolate the other samples.
 * We use the previous batch timestamp and our current batch timestamp
 * as a way to calculate period, then spread the samples evenly.
 *
 * s0 int, 0ms
 * s1 int, 10ms
 * s2 int, 20ms
 * 30ms point goes by, no interrupt, previous one is still asserted
 * downloading s2 and s3
 * s3 sample, 20ms (incorrect timestamp)
 * s4 int, 40ms
 *
 * The batches are [(s0), (s1), (s2, s3), (s4)]. Since the 3rd batch
 * has 2 samples in them, we adjust the timestamp of s3.
 * s2 - s1 = 10ms, so s3 must be s2 + 10ms => 20ms. If s1 would have
 * been part of a bigger batch things would have gotten a little
 * more complicated.
 *
 * Note: we also assume another sensor sample doesn't break up a batch
 * in 2 or more partitions. Example, there can't ever be a sync sensor
 * in between S2 and S3. This simplifies the following code.
 */
static void
cros_ec_sensor_ring_spread_add(struct cros_ec_sensorhub *sensorhub,
			       unsigned long sensor_mask,
			       struct cros_ec_sensors_ring_sample *last_out)
{
	struct cros_ec_sensors_ring_sample *batch_start, *next_batch_start;
	int id;

	for_each_set_bit(id, &sensor_mask, sensorhub->sensor_num) {
		for (batch_start = sensorhub->ring; batch_start < last_out;
		     batch_start = next_batch_start) {
			/*
			 * For each batch (where all samples have the same
			 * timestamp).
			 */
			int batch_len, sample_idx;
			struct cros_ec_sensors_ring_sample *batch_end =
				batch_start;
			struct cros_ec_sensors_ring_sample *s;
			s64 batch_timestamp = batch_start->timestamp;
			s64 sample_period;

			/*
			 * Skip over batches that start with the sensor types
			 * we're not looking at right now.
			 */
			if (batch_start->sensor_id != id) {
				next_batch_start = batch_start + 1;
				continue;
			}

			/*
			 * Do not start a batch
			 * from a flush, as it happens asynchronously to the
			 * regular flow of events.
			 */
			if (batch_start->flag & MOTIONSENSE_SENSOR_FLAG_FLUSH) {
				cros_sensorhub_send_sample(sensorhub,
							   batch_start);
				next_batch_start = batch_start + 1;
				continue;
			}

			if (batch_start->timestamp <=
			    sensorhub->batch_state[id].last_ts) {
				batch_timestamp =
					sensorhub->batch_state[id].last_ts;
				batch_len = sensorhub->batch_state[id].last_len;

				sample_idx = batch_len;

				sensorhub->batch_state[id].last_ts =
				  sensorhub->batch_state[id].penul_ts;
				sensorhub->batch_state[id].last_len =
				  sensorhub->batch_state[id].penul_len;
			} else {
				/*
				 * Push first sample in the batch to the,
				 * kifo, it's guaranteed to be correct, the
				 * rest will follow later on.
				 */
				sample_idx = 1;
				batch_len = 1;
				cros_sensorhub_send_sample(sensorhub,
							   batch_start);
				batch_start++;
			}

			/* Find all samples have the same timestamp. */
			for (s = batch_start; s < last_out; s++) {
				if (s->sensor_id != id)
					/*
					 * Skip over other sensor types that
					 * are interleaved, don't count them.
					 */
					continue;
				if (s->timestamp != batch_timestamp)
					/* we discovered the next batch */
					break;
				if (s->flag & MOTIONSENSE_SENSOR_FLAG_FLUSH)
					/* break on flush packets */
					break;
				batch_end = s;
				batch_len++;
			}

			if (batch_len == 1)
				goto done_with_this_batch;

			/* Can we calculate period? */
			if (sensorhub->batch_state[id].last_len == 0) {
				dev_warn(sensorhub->dev, "Sensor %d: lost %d samples when spreading\n",
					 id, batch_len - 1);
				goto done_with_this_batch;
				/*
				 * Note: we're dropping the rest of the samples
				 * in this batch since we have no idea where
				 * they're supposed to go without a period
				 * calculation.
				 */
			}

			sample_period = div_s64(batch_timestamp -
				sensorhub->batch_state[id].last_ts,
				sensorhub->batch_state[id].last_len);
			dev_dbg(sensorhub->dev,
				"Adjusting %d samples, sensor %d last_batch @%lld (%d samples) batch_timestamp=%lld => period=%lld\n",
				batch_len, id,
				sensorhub->batch_state[id].last_ts,
				sensorhub->batch_state[id].last_len,
				batch_timestamp,
				sample_period);

			/*
			 * Adjust timestamps of the samples then push them to
			 * kfifo.
			 */
			for (s = batch_start; s <= batch_end; s++) {
				if (s->sensor_id != id)
					/*
					 * Skip over other sensor types that
					 * are interleaved, don't change them.
					 */
					continue;

				s->timestamp = batch_timestamp +
					sample_period * sample_idx;
				sample_idx++;

				cros_sensorhub_send_sample(sensorhub, s);
			}

done_with_this_batch:
			sensorhub->batch_state[id].penul_ts =
				sensorhub->batch_state[id].last_ts;
			sensorhub->batch_state[id].penul_len =
				sensorhub->batch_state[id].last_len;

			sensorhub->batch_state[id].last_ts =
				batch_timestamp;
			sensorhub->batch_state[id].last_len = batch_len;

			next_batch_start = batch_end + 1;
		}
	}
}

/*
 * cros_ec_sensor_ring_spread_add_legacy: Calculate proper timestamps then
 * add to ringbuffer (legacy).
 *
 * Note: This assumes we're running old firmware, where timestamp
 * is inserted after its sample(s)e. There can be several samples between
 * timestamps, so several samples can have the same timestamp.
 *
 *                        timestamp | count
 *                        -----------------
 *          1st sample --> TS1      | 1
 *                         TS2      | 2
 *                         TS2      | 3
 *                         TS3      | 4
 *           last_out -->
 *
 *
 * We spread time for the samples using perod p = (current - TS1)/4.
 * between TS1 and TS2: [TS1+p/4, TS1+2p/4, TS1+3p/4, current_timestamp].
 *
 */
static void
cros_ec_sensor_ring_spread_add_legacy(struct cros_ec_sensorhub *sensorhub,
				      unsigned long sensor_mask,
				      s64 current_timestamp,
				      struct cros_ec_sensors_ring_sample
				      *last_out)
{
	struct cros_ec_sensors_ring_sample *out;
	int i;

	for_each_set_bit(i, &sensor_mask, sensorhub->sensor_num) {
		s64 timestamp;
		int count = 0;
		s64 time_period;

		for (out = sensorhub->ring; out < last_out; out++) {
			if (out->sensor_id != i)
				continue;

			/* Timestamp to start with */
			timestamp = out->timestamp;
			out++;
			count = 1;
			break;
		}
		for (; out < last_out; out++) {
			/* Find last sample. */
			if (out->sensor_id != i)
				continue;
			count++;
		}
		if (count == 0)
			continue;

		/* Spread uniformly between the first and last samples. */
		time_period = div_s64(current_timestamp - timestamp, count);

		for (out = sensorhub->ring; out < last_out; out++) {
			if (out->sensor_id != i)
				continue;
			timestamp += time_period;
			out->timestamp = timestamp;
		}
	}

	/* Push the event into the kfifo */
	for (out = sensorhub->ring; out < last_out; out++)
		cros_sensorhub_send_sample(sensorhub, out);
}

/**
 * cros_ec_sensorhub_ring_handler() - The trigger handler function
 *
 * @sensorhub: Sensor Hub object.
 *
 * Called by the notifier, process the EC sensor FIFO queue.
 */
static void cros_ec_sensorhub_ring_handler(struct cros_ec_sensorhub *sensorhub)
{
	struct ec_response_motion_sense_fifo_info *fifo_info =
		sensorhub->fifo_info;
	struct cros_ec_dev *ec = sensorhub->ec;
	ktime_t fifo_timestamp, current_timestamp;
	int i, j, number_data, ret;
	unsigned long sensor_mask = 0;
	struct ec_response_motion_sensor_data *in;
	struct cros_ec_sensors_ring_sample *out, *last_out;

	mutex_lock(&sensorhub->cmd_lock);

	/* Get FIFO information if there are lost vectors. */
	if (fifo_info->total_lost) {
		int fifo_info_length =
			sizeof(struct ec_response_motion_sense_fifo_info) +
			sizeof(u16) * sensorhub->sensor_num;

		/* Need to retrieve the number of lost vectors per sensor */
		sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_INFO;
		sensorhub->msg->outsize = 1;
		sensorhub->msg->insize = fifo_info_length;

		if (cros_ec_cmd_xfer_status(ec->ec_dev, sensorhub->msg) < 0)
			goto error;

		memcpy(fifo_info, &sensorhub->resp->fifo_info,
		       fifo_info_length);

		/*
		 * Update collection time, will not be as precise as the
		 * non-error case.
		 */
		fifo_timestamp = cros_ec_get_time_ns();
	} else {
		fifo_timestamp = sensorhub->fifo_timestamp[
			CROS_EC_SENSOR_NEW_TS];
	}

	if (fifo_info->count > sensorhub->fifo_size ||
	    fifo_info->size != sensorhub->fifo_size) {
		dev_warn(sensorhub->dev,
			 "Mismatch EC data: count %d, size %d - expected %d\n",
			 fifo_info->count, fifo_info->size,
			 sensorhub->fifo_size);
		goto error;
	}

	/* Copy elements in the main fifo */
	current_timestamp = sensorhub->fifo_timestamp[CROS_EC_SENSOR_LAST_TS];
	out = sensorhub->ring;
	for (i = 0; i < fifo_info->count; i += number_data) {
		sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_READ;
		sensorhub->params->fifo_read.max_data_vector =
			fifo_info->count - i;
		sensorhub->msg->outsize =
			sizeof(struct ec_params_motion_sense);
		sensorhub->msg->insize =
			sizeof(sensorhub->resp->fifo_read) +
			sensorhub->params->fifo_read.max_data_vector *
			  sizeof(struct ec_response_motion_sensor_data);
		ret = cros_ec_cmd_xfer_status(ec->ec_dev, sensorhub->msg);
		if (ret < 0) {
			dev_warn(sensorhub->dev, "Fifo error: %d\n", ret);
			break;
		}
		number_data = sensorhub->resp->fifo_read.number_data;
		if (number_data == 0) {
			dev_dbg(sensorhub->dev, "Unexpected empty FIFO\n");
			break;
		}
		if (number_data > fifo_info->count - i) {
			dev_warn(sensorhub->dev,
				 "Invalid EC data: too many entry received: %d, expected %d\n",
				 number_data, fifo_info->count - i);
			break;
		}
		if (out + number_data >
		    sensorhub->ring + fifo_info->count) {
			dev_warn(sensorhub->dev,
				 "Too many samples: %d (%zd data) to %d entries for expected %d entries\n",
				 i, out - sensorhub->ring, i + number_data,
				 fifo_info->count);
			break;
		}

		for (in = sensorhub->resp->fifo_read.data, j = 0;
		     j < number_data; j++, in++) {
			if (cros_ec_sensor_ring_process_event(
						sensorhub, fifo_info,
						fifo_timestamp,
						&current_timestamp,
						in, out)) {
				sensor_mask |= BIT(in->sensor_num);
				out++;
			}
		}
	}
	mutex_unlock(&sensorhub->cmd_lock);
	last_out = out;

	if (out == sensorhub->ring)
		/* Unexpected empty FIFO. */
		goto ring_handler_end;

	/*
	 * Check if current_timestamp is ahead of the last sample. Normally,
	 * the EC appends a timestamp after the last sample, but if the AP
	 * is slow to respond to the IRQ, the EC may have added new samples.
	 * Use the FIFO info timestamp as last timestamp then.
	 */
	if (!sensorhub->tight_timestamps &&
	    (last_out - 1)->timestamp == current_timestamp)
		current_timestamp = fifo_timestamp;

	/* Warn on lost samples. */
	if (fifo_info->total_lost)
		for (i = 0; i < sensorhub->sensor_num; i++) {
			if (fifo_info->lost[i]) {
				dev_warn_ratelimited(sensorhub->dev,
						     "Sensor %d: lost: %d out of %d\n",
						     i, fifo_info->lost[i],
						     fifo_info->total_lost);
				if (sensorhub->tight_timestamps)
					sensorhub->batch_state[i].last_len = 0;
			}
		}

	/*
	 * Spread samples in case of batching, then add them to the
	 * ringbuffer.
	 */
	if (sensorhub->tight_timestamps)
		cros_ec_sensor_ring_spread_add(sensorhub, sensor_mask,
					       last_out);
	else
		cros_ec_sensor_ring_spread_add_legacy(sensorhub, sensor_mask,
						      current_timestamp,
						      last_out);

ring_handler_end:
	sensorhub->fifo_timestamp[CROS_EC_SENSOR_LAST_TS] = current_timestamp;
	return;

error:
	mutex_unlock(&sensorhub->cmd_lock);
}

static int cros_ec_sensorhub_event(struct notifier_block *nb,
				   unsigned long queued_during_suspend,
				   void *_notify)
{
	struct cros_ec_sensorhub *sensorhub;
	struct cros_ec_device *ec_dev;

	sensorhub = container_of(nb, struct cros_ec_sensorhub, notifier);
	ec_dev = sensorhub->ec->ec_dev;

	if (ec_dev->event_data.event_type != EC_MKBP_EVENT_SENSOR_FIFO)
		return NOTIFY_DONE;

	if (ec_dev->event_size != sizeof(ec_dev->event_data.data.sensor_fifo)) {
		dev_warn(ec_dev->dev, "Invalid fifo info size\n");
		return NOTIFY_DONE;
	}

	if (queued_during_suspend)
		return NOTIFY_OK;

	memcpy(sensorhub->fifo_info, &ec_dev->event_data.data.sensor_fifo.info,
	       sizeof(*sensorhub->fifo_info));
	sensorhub->fifo_timestamp[CROS_EC_SENSOR_NEW_TS] =
		ec_dev->last_event_time;
	cros_ec_sensorhub_ring_handler(sensorhub);

	return NOTIFY_OK;
}

/**
 * cros_ec_sensorhub_ring_allocate() - Prepare the FIFO functionality if the EC
 *				       supports it.
 *
 * @sensorhub : Sensor Hub object.
 *
 * Return: 0 on success.
 */
int cros_ec_sensorhub_ring_allocate(struct cros_ec_sensorhub *sensorhub)
{
	int fifo_info_length =
		sizeof(struct ec_response_motion_sense_fifo_info) +
		sizeof(u16) * sensorhub->sensor_num;

	/* Allocate the array for lost events. */
	sensorhub->fifo_info = devm_kzalloc(sensorhub->dev, fifo_info_length,
					    GFP_KERNEL);
	if (!sensorhub->fifo_info)
		return -ENOMEM;

	/*
	 * Allocate the callback area based on the number of sensors.
	 * Add one for the sensor ring.
	 */
	sensorhub->push_data = devm_kcalloc(sensorhub->dev,
			sensorhub->sensor_num,
			sizeof(*sensorhub->push_data),
			GFP_KERNEL);
	if (!sensorhub->push_data)
		return -ENOMEM;

	sensorhub->tight_timestamps = cros_ec_check_features(
			sensorhub->ec,
			EC_FEATURE_MOTION_SENSE_TIGHT_TIMESTAMPS);

	if (sensorhub->tight_timestamps) {
		sensorhub->batch_state = devm_kcalloc(sensorhub->dev,
				sensorhub->sensor_num,
				sizeof(*sensorhub->batch_state),
				GFP_KERNEL);
		if (!sensorhub->batch_state)
			return -ENOMEM;
	}

	return 0;
}

/**
 * cros_ec_sensorhub_ring_add() - Add the FIFO functionality if the EC
 *				  supports it.
 *
 * @sensorhub : Sensor Hub object.
 *
 * Return: 0 on success.
 */
int cros_ec_sensorhub_ring_add(struct cros_ec_sensorhub *sensorhub)
{
	struct cros_ec_dev *ec = sensorhub->ec;
	int ret;
	int fifo_info_length =
		sizeof(struct ec_response_motion_sense_fifo_info) +
		sizeof(u16) * sensorhub->sensor_num;

	/* Retrieve FIFO information */
	sensorhub->msg->version = 2;
	sensorhub->params->cmd = MOTIONSENSE_CMD_FIFO_INFO;
	sensorhub->msg->outsize = 1;
	sensorhub->msg->insize = fifo_info_length;

	ret = cros_ec_cmd_xfer_status(ec->ec_dev, sensorhub->msg);
	if (ret < 0)
		return ret;

	/*
	 * Allocate the full fifo. We need to copy the whole FIFO to set
	 * timestamps properly.
	 */
	sensorhub->fifo_size = sensorhub->resp->fifo_info.size;
	sensorhub->ring = devm_kcalloc(sensorhub->dev, sensorhub->fifo_size,
				       sizeof(*sensorhub->ring), GFP_KERNEL);
	if (!sensorhub->ring)
		return -ENOMEM;

	sensorhub->fifo_timestamp[CROS_EC_SENSOR_LAST_TS] =
		cros_ec_get_time_ns();

	/* Register the notifier that will act as a top half interrupt. */
	sensorhub->notifier.notifier_call = cros_ec_sensorhub_event;
	ret = blocking_notifier_chain_register(&ec->ec_dev->event_notifier,
					       &sensorhub->notifier);
	if (ret < 0)
		return ret;

	/* Start collection samples. */
	return cros_ec_sensorhub_ring_fifo_enable(sensorhub, true);
}

void cros_ec_sensorhub_ring_remove(void *arg)
{
	struct cros_ec_sensorhub *sensorhub = arg;
	struct cros_ec_device *ec_dev = sensorhub->ec->ec_dev;

	/* Disable the ring, prevent EC interrupt to the AP for nothing. */
	cros_ec_sensorhub_ring_fifo_enable(sensorhub, false);
	blocking_notifier_chain_unregister(&ec_dev->event_notifier,
					   &sensorhub->notifier);
}
