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
	struct {
		struct nvif_event_v0 base;
		struct nvif_conn_event_v0 conn;
	} args;
	int ret;

	args.conn.version = 0;
	args.conn.types = types;

	ret = nvif_event_ctor_(&conn->object, name ?: "nvifConnHpd", nvif_conn_id(conn),
			       func, true, &args.base, sizeof(args), false, event);
	NVIF_DEBUG(&conn->object, "[NEW EVENT:HPD types:%02x]", types);
	return ret;
}

int
nvif_conn_hpd_status(struct nvif_conn *conn)
{
	struct nvif_conn_hpd_status_v0 args;
	int ret;

	args.version = 0;

	ret = nvif_mthd(&conn->object, NVIF_CONN_V0_HPD_STATUS, &args, sizeof(args));
	NVIF_ERRON(ret, &conn->object, "[HPD_STATUS] support:%d present:%d",
		   args.support, args.present);
	return ret ? ret : !!args.support + !!args.present;
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
	return ret;
}
