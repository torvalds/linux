/**
 * Copyright (C) ARM Limited 2012-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define ESCAPE_CODE 0x1c
#define STRING_ANNOTATION 0x06
#define NAME_CHANNEL_ANNOTATION 0x07
#define NAME_GROUP_ANNOTATION 0x08
#define VISUAL_ANNOTATION 0x04
#define MARKER_ANNOTATION 0x05

static void kannotate_write(const char *ptr, unsigned int size)
{
	int retval;
	int pos = 0;
	loff_t offset = 0;

	while (pos < size) {
		retval = annotate_write(NULL, &ptr[pos], size - pos, &offset);
		if (retval < 0) {
			pr_warning("gator: kannotate_write failed with return value %d\n", retval);
			return;
		}
		pos += retval;
	}
}

static void marshal_u16(char *buf, u16 val)
{
	buf[0] = val & 0xff;
	buf[1] = (val >> 8) & 0xff;
}

static void marshal_u32(char *buf, u32 val)
{
	buf[0] = val & 0xff;
	buf[1] = (val >> 8) & 0xff;
	buf[2] = (val >> 16) & 0xff;
	buf[3] = (val >> 24) & 0xff;
}

void gator_annotate_channel(int channel, const char *str)
{
	const u16 str_size = strlen(str) & 0xffff;
	char header[8];

	header[0] = ESCAPE_CODE;
	header[1] = STRING_ANNOTATION;
	marshal_u32(header + 2, channel);
	marshal_u16(header + 6, str_size);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size);
}
EXPORT_SYMBOL(gator_annotate_channel);

void gator_annotate(const char *str)
{
	gator_annotate_channel(0, str);
}
EXPORT_SYMBOL(gator_annotate);

void gator_annotate_channel_color(int channel, int color, const char *str)
{
	const u16 str_size = (strlen(str) + 4) & 0xffff;
	char header[12];

	header[0] = ESCAPE_CODE;
	header[1] = STRING_ANNOTATION;
	marshal_u32(header + 2, channel);
	marshal_u16(header + 6, str_size);
	marshal_u32(header + 8, color);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size - 4);
}
EXPORT_SYMBOL(gator_annotate_channel_color);

void gator_annotate_color(int color, const char *str)
{
	gator_annotate_channel_color(0, color, str);
}
EXPORT_SYMBOL(gator_annotate_color);

void gator_annotate_channel_end(int channel)
{
	char header[8];

	header[0] = ESCAPE_CODE;
	header[1] = STRING_ANNOTATION;
	marshal_u32(header + 2, channel);
	marshal_u16(header + 6, 0);
	kannotate_write(header, sizeof(header));
}
EXPORT_SYMBOL(gator_annotate_channel_end);

void gator_annotate_end(void)
{
	gator_annotate_channel_end(0);
}
EXPORT_SYMBOL(gator_annotate_end);

void gator_annotate_name_channel(int channel, int group, const char *str)
{
	const u16 str_size = strlen(str) & 0xffff;
	char header[12];

	header[0] = ESCAPE_CODE;
	header[1] = NAME_CHANNEL_ANNOTATION;
	marshal_u32(header + 2, channel);
	marshal_u32(header + 6, group);
	marshal_u16(header + 10, str_size);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size);
}
EXPORT_SYMBOL(gator_annotate_name_channel);

void gator_annotate_name_group(int group, const char *str)
{
	const u16 str_size = strlen(str) & 0xffff;
	char header[8];

	header[0] = ESCAPE_CODE;
	header[1] = NAME_GROUP_ANNOTATION;
	marshal_u32(header + 2, group);
	marshal_u16(header + 6, str_size);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size);
}
EXPORT_SYMBOL(gator_annotate_name_group);

void gator_annotate_visual(const char *data, unsigned int length, const char *str)
{
	const u16 str_size = strlen(str) & 0xffff;
	char header[4];
	char header_length[4];

	header[0] = ESCAPE_CODE;
	header[1] = VISUAL_ANNOTATION;
	marshal_u16(header + 2, str_size);
	marshal_u32(header_length, length);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size);
	kannotate_write(header_length, sizeof(header_length));
	kannotate_write(data, length);
}
EXPORT_SYMBOL(gator_annotate_visual);

void gator_annotate_marker(void)
{
	char header[4];

	header[0] = ESCAPE_CODE;
	header[1] = MARKER_ANNOTATION;
	marshal_u16(header + 2, 0);
	kannotate_write(header, sizeof(header));
}
EXPORT_SYMBOL(gator_annotate_marker);

void gator_annotate_marker_str(const char *str)
{
	const u16 str_size = strlen(str) & 0xffff;
	char header[4];

	header[0] = ESCAPE_CODE;
	header[1] = MARKER_ANNOTATION;
	marshal_u16(header + 2, str_size);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size);
}
EXPORT_SYMBOL(gator_annotate_marker_str);

void gator_annotate_marker_color(int color)
{
	char header[8];

	header[0] = ESCAPE_CODE;
	header[1] = MARKER_ANNOTATION;
	marshal_u16(header + 2, 4);
	marshal_u32(header + 4, color);
	kannotate_write(header, sizeof(header));
}
EXPORT_SYMBOL(gator_annotate_marker_color);

void gator_annotate_marker_color_str(int color, const char *str)
{
	const u16 str_size = (strlen(str) + 4) & 0xffff;
	char header[8];

	header[0] = ESCAPE_CODE;
	header[1] = MARKER_ANNOTATION;
	marshal_u16(header + 2, str_size);
	marshal_u32(header + 4, color);
	kannotate_write(header, sizeof(header));
	kannotate_write(str, str_size - 4);
}
EXPORT_SYMBOL(gator_annotate_marker_color_str);
