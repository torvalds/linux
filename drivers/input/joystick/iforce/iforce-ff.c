/*
 * $Id: iforce-ff.c,v 1.9 2002/02/02 19:28:35 jdeneux Exp $
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik <vojtech@ucw.cz>
 *  Copyright (c) 2001-2002 Johann Deneux <deneux@ifrance.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include "iforce.h"

/*
 * Set the magnitude of a constant force effect
 * Return error code
 *
 * Note: caller must ensure exclusive access to device
 */

static int make_magnitude_modifier(struct iforce* iforce,
	struct resource* mod_chunk, int no_alloc, __s16 level)
{
	unsigned char data[3];

	if (!no_alloc) {
		mutex_lock(&iforce->mem_mutex);
		if (allocate_resource(&(iforce->device_memory), mod_chunk, 2,
			iforce->device_memory.start, iforce->device_memory.end, 2L,
			NULL, NULL)) {
			mutex_unlock(&iforce->mem_mutex);
			return -ENOMEM;
		}
		mutex_unlock(&iforce->mem_mutex);
	}

	data[0] = LO(mod_chunk->start);
	data[1] = HI(mod_chunk->start);
	data[2] = HIFIX80(level);

	iforce_send_packet(iforce, FF_CMD_MAGNITUDE, data);

	iforce_dump_packet("magnitude: ", FF_CMD_MAGNITUDE, data);
	return 0;
}

/*
 * Upload the component of an effect dealing with the period, phase and magnitude
 */

static int make_period_modifier(struct iforce* iforce,
	struct resource* mod_chunk, int no_alloc,
	__s16 magnitude, __s16 offset, u16 period, u16 phase)
{
	unsigned char data[7];

	period = TIME_SCALE(period);

	if (!no_alloc) {
		mutex_lock(&iforce->mem_mutex);
		if (allocate_resource(&(iforce->device_memory), mod_chunk, 0x0c,
			iforce->device_memory.start, iforce->device_memory.end, 2L,
			NULL, NULL)) {
			mutex_unlock(&iforce->mem_mutex);
			return -ENOMEM;
		}
		mutex_unlock(&iforce->mem_mutex);
	}

	data[0] = LO(mod_chunk->start);
	data[1] = HI(mod_chunk->start);

	data[2] = HIFIX80(magnitude);
	data[3] = HIFIX80(offset);
	data[4] = HI(phase);

	data[5] = LO(period);
	data[6] = HI(period);

	iforce_send_packet(iforce, FF_CMD_PERIOD, data);

	return 0;
}

/*
 * Uploads the part of an effect setting the envelope of the force
 */

static int make_envelope_modifier(struct iforce* iforce,
	struct resource* mod_chunk, int no_alloc,
	u16 attack_duration, __s16 initial_level,
	u16 fade_duration, __s16 final_level)
{
	unsigned char data[8];

	attack_duration = TIME_SCALE(attack_duration);
	fade_duration = TIME_SCALE(fade_duration);

	if (!no_alloc) {
		mutex_lock(&iforce->mem_mutex);
		if (allocate_resource(&(iforce->device_memory), mod_chunk, 0x0e,
			iforce->device_memory.start, iforce->device_memory.end, 2L,
			NULL, NULL)) {
			mutex_unlock(&iforce->mem_mutex);
			return -ENOMEM;
		}
		mutex_unlock(&iforce->mem_mutex);
	}

	data[0] = LO(mod_chunk->start);
	data[1] = HI(mod_chunk->start);

	data[2] = LO(attack_duration);
	data[3] = HI(attack_duration);
	data[4] = HI(initial_level);

	data[5] = LO(fade_duration);
	data[6] = HI(fade_duration);
	data[7] = HI(final_level);

	iforce_send_packet(iforce, FF_CMD_ENVELOPE, data);

	return 0;
}

/*
 * Component of spring, friction, inertia... effects
 */

static int make_condition_modifier(struct iforce* iforce,
	struct resource* mod_chunk, int no_alloc,
	__u16 rsat, __u16 lsat, __s16 rk, __s16 lk, u16 db, __s16 center)
{
	unsigned char data[10];

	if (!no_alloc) {
		mutex_lock(&iforce->mem_mutex);
		if (allocate_resource(&(iforce->device_memory), mod_chunk, 8,
			iforce->device_memory.start, iforce->device_memory.end, 2L,
			NULL, NULL)) {
			mutex_unlock(&iforce->mem_mutex);
			return -ENOMEM;
		}
		mutex_unlock(&iforce->mem_mutex);
	}

	data[0] = LO(mod_chunk->start);
	data[1] = HI(mod_chunk->start);

	data[2] = (100*rk)>>15;	/* Dangerous: the sign is extended by gcc on plateforms providing an arith shift */
	data[3] = (100*lk)>>15; /* This code is incorrect on cpus lacking arith shift */

	center = (500*center)>>15;
	data[4] = LO(center);
	data[5] = HI(center);

	db = (1000*db)>>16;
	data[6] = LO(db);
	data[7] = HI(db);

	data[8] = (100*rsat)>>16;
	data[9] = (100*lsat)>>16;

	iforce_send_packet(iforce, FF_CMD_CONDITION, data);
	iforce_dump_packet("condition", FF_CMD_CONDITION, data);

	return 0;
}

static unsigned char find_button(struct iforce *iforce, signed short button)
{
	int i;
	for (i = 1; iforce->type->btn[i] >= 0; i++)
		if (iforce->type->btn[i] == button)
			return i + 1;
	return 0;
}

/*
 * Analyse the changes in an effect, and tell if we need to send an condition
 * parameter packet
 */
static int need_condition_modifier(struct iforce* iforce, struct ff_effect* new)
{
	int id = new->id;
	struct ff_effect* old = &iforce->core_effects[id].effect;
	int ret=0;
	int i;

	if (new->type != FF_SPRING && new->type != FF_FRICTION) {
		printk(KERN_WARNING "iforce.c: bad effect type in need_condition_modifier\n");
		return FALSE;
	}

	for(i=0; i<2; i++) {
		ret |= old->u.condition[i].right_saturation != new->u.condition[i].right_saturation
			|| old->u.condition[i].left_saturation != new->u.condition[i].left_saturation
			|| old->u.condition[i].right_coeff != new->u.condition[i].right_coeff
			|| old->u.condition[i].left_coeff != new->u.condition[i].left_coeff
			|| old->u.condition[i].deadband != new->u.condition[i].deadband
			|| old->u.condition[i].center != new->u.condition[i].center;
	}
	return ret;
}

/*
 * Analyse the changes in an effect, and tell if we need to send a magnitude
 * parameter packet
 */
static int need_magnitude_modifier(struct iforce* iforce, struct ff_effect* effect)
{
	int id = effect->id;
	struct ff_effect* old = &iforce->core_effects[id].effect;

	if (effect->type != FF_CONSTANT) {
		printk(KERN_WARNING "iforce.c: bad effect type in need_envelope_modifier\n");
		return FALSE;
	}

	return (old->u.constant.level != effect->u.constant.level);
}

/*
 * Analyse the changes in an effect, and tell if we need to send an envelope
 * parameter packet
 */
static int need_envelope_modifier(struct iforce* iforce, struct ff_effect* effect)
{
	int id = effect->id;
	struct ff_effect* old = &iforce->core_effects[id].effect;

	switch (effect->type) {
	case FF_CONSTANT:
		if (old->u.constant.envelope.attack_length != effect->u.constant.envelope.attack_length
		|| old->u.constant.envelope.attack_level != effect->u.constant.envelope.attack_level
		|| old->u.constant.envelope.fade_length != effect->u.constant.envelope.fade_length
		|| old->u.constant.envelope.fade_level != effect->u.constant.envelope.fade_level)
			return TRUE;
		break;

	case FF_PERIODIC:
		if (old->u.periodic.envelope.attack_length != effect->u.periodic.envelope.attack_length
		|| old->u.periodic.envelope.attack_level != effect->u.periodic.envelope.attack_level
		|| old->u.periodic.envelope.fade_length != effect->u.periodic.envelope.fade_length
		|| old->u.periodic.envelope.fade_level != effect->u.periodic.envelope.fade_level)
			return TRUE;
		break;

	default:
		printk(KERN_WARNING "iforce.c: bad effect type in need_envelope_modifier\n");
	}

	return FALSE;
}

/*
 * Analyse the changes in an effect, and tell if we need to send a periodic
 * parameter effect
 */
static int need_period_modifier(struct iforce* iforce, struct ff_effect* new)
{
	int id = new->id;
	struct ff_effect* old = &iforce->core_effects[id].effect;

	if (new->type != FF_PERIODIC) {
		printk(KERN_WARNING "iforce.c: bad effect type in need_periodic_modifier\n");
		return FALSE;
	}

	return (old->u.periodic.period != new->u.periodic.period
		|| old->u.periodic.magnitude != new->u.periodic.magnitude
		|| old->u.periodic.offset != new->u.periodic.offset
		|| old->u.periodic.phase != new->u.periodic.phase);
}

/*
 * Analyse the changes in an effect, and tell if we need to send an effect
 * packet
 */
static int need_core(struct iforce* iforce, struct ff_effect* new)
{
	int id = new->id;
	struct ff_effect* old = &iforce->core_effects[id].effect;

	if (old->direction != new->direction
		|| old->trigger.button != new->trigger.button
		|| old->trigger.interval != new->trigger.interval
		|| old->replay.length != new->replay.length
		|| old->replay.delay != new->replay.delay)
		return TRUE;

	return FALSE;
}
/*
 * Send the part common to all effects to the device
 */
static int make_core(struct iforce* iforce, u16 id, u16 mod_id1, u16 mod_id2,
	u8 effect_type, u8 axes, u16 duration, u16 delay, u16 button,
	u16 interval, u16 direction)
{
	unsigned char data[14];

	duration = TIME_SCALE(duration);
	delay    = TIME_SCALE(delay);
	interval = TIME_SCALE(interval);

	data[0]  = LO(id);
	data[1]  = effect_type;
	data[2]  = LO(axes) | find_button(iforce, button);

	data[3]  = LO(duration);
	data[4]  = HI(duration);

	data[5]  = HI(direction);

	data[6]  = LO(interval);
	data[7]  = HI(interval);

	data[8]  = LO(mod_id1);
	data[9]  = HI(mod_id1);
	data[10] = LO(mod_id2);
	data[11] = HI(mod_id2);

	data[12] = LO(delay);
	data[13] = HI(delay);

	/* Stop effect */
/*	iforce_control_playback(iforce, id, 0);*/

	iforce_send_packet(iforce, FF_CMD_EFFECT, data);

	/* If needed, restart effect */
	if (test_bit(FF_CORE_SHOULD_PLAY, iforce->core_effects[id].flags)) {
		/* BUG: perhaps we should replay n times, instead of 1. But we do not know n */
		iforce_control_playback(iforce, id, 1);
	}

	return 0;
}

/*
 * Upload a periodic effect to the device
 * See also iforce_upload_constant.
 */
int iforce_upload_periodic(struct iforce* iforce, struct ff_effect* effect, int is_update)
{
	u8 wave_code;
	int core_id = effect->id;
	struct iforce_core_effect* core_effect = iforce->core_effects + core_id;
	struct resource* mod1_chunk = &(iforce->core_effects[core_id].mod1_chunk);
	struct resource* mod2_chunk = &(iforce->core_effects[core_id].mod2_chunk);
	int param1_err = 1;
	int param2_err = 1;
	int core_err = 0;

	if (!is_update || need_period_modifier(iforce, effect)) {
		param1_err = make_period_modifier(iforce, mod1_chunk,
			is_update,
			effect->u.periodic.magnitude, effect->u.periodic.offset,
			effect->u.periodic.period, effect->u.periodic.phase);
		if (param1_err) return param1_err;
		set_bit(FF_MOD1_IS_USED, core_effect->flags);
	}

	if (!is_update || need_envelope_modifier(iforce, effect)) {
		param2_err = make_envelope_modifier(iforce, mod2_chunk,
			is_update,
			effect->u.periodic.envelope.attack_length,
			effect->u.periodic.envelope.attack_level,
			effect->u.periodic.envelope.fade_length,
			effect->u.periodic.envelope.fade_level);
		if (param2_err) return param2_err;
		set_bit(FF_MOD2_IS_USED, core_effect->flags);
	}

	switch (effect->u.periodic.waveform) {
		case FF_SQUARE:		wave_code = 0x20; break;
		case FF_TRIANGLE:	wave_code = 0x21; break;
		case FF_SINE:		wave_code = 0x22; break;
		case FF_SAW_UP:		wave_code = 0x23; break;
		case FF_SAW_DOWN:	wave_code = 0x24; break;
		default:		wave_code = 0x20; break;
	}

	if (!is_update || need_core(iforce, effect)) {
		core_err = make_core(iforce, effect->id,
			mod1_chunk->start,
			mod2_chunk->start,
			wave_code,
			0x20,
			effect->replay.length,
			effect->replay.delay,
			effect->trigger.button,
			effect->trigger.interval,
			effect->direction);
	}

	/* If one of the parameter creation failed, we already returned an
	 * error code.
	 * If the core creation failed, we return its error code.
	 * Else: if one parameter at least was created, we return 0
	 *       else we return 1;
	 */
	return core_err < 0 ? core_err : (param1_err && param2_err);
}

/*
 * Upload a constant force effect
 * Return value:
 *  <0 Error code
 *  0 Ok, effect created or updated
 *  1 effect did not change since last upload, and no packet was therefore sent
 */
int iforce_upload_constant(struct iforce* iforce, struct ff_effect* effect, int is_update)
{
	int core_id = effect->id;
	struct iforce_core_effect* core_effect = iforce->core_effects + core_id;
	struct resource* mod1_chunk = &(iforce->core_effects[core_id].mod1_chunk);
	struct resource* mod2_chunk = &(iforce->core_effects[core_id].mod2_chunk);
	int param1_err = 1;
	int param2_err = 1;
	int core_err = 0;

	if (!is_update || need_magnitude_modifier(iforce, effect)) {
		param1_err = make_magnitude_modifier(iforce, mod1_chunk,
			is_update,
			effect->u.constant.level);
		if (param1_err) return param1_err;
		set_bit(FF_MOD1_IS_USED, core_effect->flags);
	}

	if (!is_update || need_envelope_modifier(iforce, effect)) {
		param2_err = make_envelope_modifier(iforce, mod2_chunk,
			is_update,
			effect->u.constant.envelope.attack_length,
			effect->u.constant.envelope.attack_level,
			effect->u.constant.envelope.fade_length,
			effect->u.constant.envelope.fade_level);
		if (param2_err) return param2_err;
		set_bit(FF_MOD2_IS_USED, core_effect->flags);
	}

	if (!is_update || need_core(iforce, effect)) {
		core_err = make_core(iforce, effect->id,
			mod1_chunk->start,
			mod2_chunk->start,
			0x00,
			0x20,
			effect->replay.length,
			effect->replay.delay,
			effect->trigger.button,
			effect->trigger.interval,
			effect->direction);
	}

	/* If one of the parameter creation failed, we already returned an
	 * error code.
	 * If the core creation failed, we return its error code.
	 * Else: if one parameter at least was created, we return 0
	 *       else we return 1;
	 */
	return core_err < 0 ? core_err : (param1_err && param2_err);
}

/*
 * Upload an condition effect. Those are for example friction, inertia, springs...
 */
int iforce_upload_condition(struct iforce* iforce, struct ff_effect* effect, int is_update)
{
	int core_id = effect->id;
	struct iforce_core_effect* core_effect = iforce->core_effects + core_id;
	struct resource* mod1_chunk = &(core_effect->mod1_chunk);
	struct resource* mod2_chunk = &(core_effect->mod2_chunk);
	u8 type;
	int param_err = 1;
	int core_err = 0;

	switch (effect->type) {
		case FF_SPRING:		type = 0x40; break;
		case FF_DAMPER:		type = 0x41; break;
		default: return -1;
	}

	if (!is_update || need_condition_modifier(iforce, effect)) {
		param_err = make_condition_modifier(iforce, mod1_chunk,
			is_update,
			effect->u.condition[0].right_saturation,
			effect->u.condition[0].left_saturation,
			effect->u.condition[0].right_coeff,
			effect->u.condition[0].left_coeff,
			effect->u.condition[0].deadband,
			effect->u.condition[0].center);
		if (param_err) return param_err;
		set_bit(FF_MOD1_IS_USED, core_effect->flags);

		param_err = make_condition_modifier(iforce, mod2_chunk,
			is_update,
			effect->u.condition[1].right_saturation,
			effect->u.condition[1].left_saturation,
			effect->u.condition[1].right_coeff,
			effect->u.condition[1].left_coeff,
			effect->u.condition[1].deadband,
			effect->u.condition[1].center);
		if (param_err) return param_err;
		set_bit(FF_MOD2_IS_USED, core_effect->flags);

	}

	if (!is_update || need_core(iforce, effect)) {
		core_err = make_core(iforce, effect->id,
			mod1_chunk->start, mod2_chunk->start,
			type, 0xc0,
			effect->replay.length, effect->replay.delay,
			effect->trigger.button, effect->trigger.interval,
			effect->direction);
	}

	/* If the parameter creation failed, we already returned an
	 * error code.
	 * If the core creation failed, we return its error code.
	 * Else: if a parameter  was created, we return 0
	 *       else we return 1;
	 */
	return core_err < 0 ? core_err : param_err;
}
