/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include <nvif/conn.h>
#include <nvif/disp.h>
#include <nvif/printf.h>

#include <nvif/class.h>
#include <nvif/if0011.h>

int
nvif_conn_event_ctor(struct nvif_conn *conn, const char *name, nvif_event_func func, u8 types,
		     struct nvif_event *event)
{
	DEFINE_RAW_FLEX(struct nvif_event_v0, args, data,
			sizeof(struct nvif_conn_event_v0));
	struct nvif_conn_event_v0 *args_conn =
				(struct nvif_conn_event_v0 *)args->data;
	int ret;

	args_conn->version = 0;
	args_conn->types = types;

	ret = nvif_event_ctor_(&conn->object, name ?: "nvifConnHpd", nvif_conn_id(conn),
			       func, true, args, __struct_size(args), false, event);
	NVIF_DEBUG(&conn->object, "[NEW EVENT:HPD types:%02x]", types);
	return ret;
}

void
nvif_conn_dtor(struct nvif_conn *conn)
{
	nvif_object_dtor(&conn->object);
}

int
nvif_conn_ctor(struct nvif_disp *disp, const char *name, int id, struct nvif_conn *conn)
{
	struct nvif_conn_v0 args;
	int ret;

	args.version = 0;
	args.id = id;

	ret = nvif_object_ctor(&disp->object, name ?: "nvifConn", id, NVIF_CLASS_CONN,
			       &args, sizeof(args), &conn->object);
	NVIF_ERRON(ret, &disp->object, "[NEW conn id:%d]", id);
	if (ret)
		return ret;

	conn->id = id;

	switch (args.type) {
	case NVIF_CONN_V0_VGA      : conn->info.type = NVIF_CONN_VGA; break;
	case NVIF_CONN_V0_TV       : conn->info.type = NVIF_CONN_TV; break;
	case NVIF_CONN_V0_DVI_I    : conn->info.type = NVIF_CONN_DVI_I; break;
	case NVIF_CONN_V0_DVI_D    : conn->info.type = NVIF_CONN_DVI_D; break;
	case NVIF_CONN_V0_LVDS     : conn->info.type = NVIF_CONN_LVDS; break;
	case NVIF_CONN_V0_LVDS_SPWG: conn->info.type = NVIF_CONN_LVDS_SPWG; break;
	case NVIF_CONN_V0_HDMI     : conn->info.type = NVIF_CONN_HDMI; break;
	case NVIF_CONN_V0_DP       : conn->info.type = NVIF_CONN_DP; break;
	case NVIF_CONN_V0_EDP      : conn->info.type = NVIF_CONN_EDP; break;
	default:
		break;
	}

	return 0;

}
