/**
 * Copyright (C) ARM Limited 2012-2013. All rights reserved.
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
			printk(KERN_WARNING "gator: kannotate_write failed with return value %d\n", retval);
			return;
		}
		pos += retval;
	}
}

void gator_annotate_channel(int channel, const char *str)
{
	int str_size = strlen(str) & 0xffff;
	long long header = ESCAPE_CODE | (STRING_ANNOTATION << 8) | (channel << 16) | ((long long)str_size << 48);
	kannotate_write((char *)&header, sizeof(header));
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
	int str_size = (strlen(str) + 4) & 0xffff;
	char header[12];
	header[0] = ESCAPE_CODE;
	header[1] = STRING_ANNOTATION;
	*(u32 *)(&header[2]) = channel;
	*(u16 *)(&header[6]) = str_size;
	*(u32 *)(&header[8]) = color;
	kannotate_write((char *)&header, sizeof(header));
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
	long long header = ESCAPE_CODE | (STRING_ANNOTATION << 8) | (channel << 16);
	kannotate_write((char *)&header, sizeof(header));
}

EXPORT_SYMBOL(gator_annotate_channel_end);

void gator_annotate_end(void)
{
	gator_annotate_channel_end(0);
}

EXPORT_SYMBOL(gator_annotate_end);

void gator_annotate_name_channel(int channel, int group, const char* str)
{
	int str_size = strlen(str) & 0xffff;
	char header[12];
	header[0] = ESCAPE_CODE;
	header[1] = NAME_CHANNEL_ANNOTATION;
	*(u32 *)(&header[2]) = channel;
	*(u32 *)(&header[6]) = group;
	*(u16 *)(&header[10]) = str_size;
	kannotate_write((char *)&header, sizeof(header));
	kannotate_write(str, str_size);
}

EXPORT_SYMBOL(gator_annotate_name_channel);

void gator_annotate_name_group(int group, const char* str)
{
	int str_size = strlen(str) & 0xffff;
	long long header = ESCAPE_CODE | (NAME_GROUP_ANNOTATION << 8) | (group << 16) | ((long long)str_size << 48);
	kannotate_write((char *)&header, sizeof(header));
	kannotate_write(str, str_size);
}

EXPORT_SYMBOL(gator_annotate_name_group);

void gator_annotate_visual(const char *data, unsigned int length, const char *str)
{
	int str_size = strlen(str) & 0xffff;
	int visual_annotation = ESCAPE_CODE | (VISUAL_ANNOTATION << 8) | (str_size << 16);
	kannotate_write((char *)&visual_annotation, sizeof(visual_annotation));
	kannotate_write(str, str_size);
	kannotate_write((char *)&length, sizeof(length));
	kannotate_write(data, length);
}

EXPORT_SYMBOL(gator_annotate_visual);

void gator_annotate_marker(void)
{
	int header = ESCAPE_CODE | (MARKER_ANNOTATION << 8);
	kannotate_write((char *)&header, sizeof(header));
}

EXPORT_SYMBOL(gator_annotate_marker);

void gator_annotate_marker_str(const char *str)
{
	int str_size = strlen(str) & 0xffff;
	int header = ESCAPE_CODE | (MARKER_ANNOTATION << 8) | (str_size << 16);
	kannotate_write((char *)&header, sizeof(header));
	kannotate_write(str, str_size);
}

EXPORT_SYMBOL(gator_annotate_marker_str);

void gator_annotate_marker_color(int color)
{
	long long header = (ESCAPE_CODE | (MARKER_ANNOTATION << 8) | 0x00040000 | ((long long)color << 32));
	kannotate_write((char *)&header, sizeof(header));
}

EXPORT_SYMBOL(gator_annotate_marker_color);

void gator_annotate_marker_color_str(int color, const char *str)
{
	int str_size = (strlen(str) + 4) & 0xffff;
	long long header = ESCAPE_CODE | (MARKER_ANNOTATION << 8) | (str_size << 16) | ((long long)color << 32);
	kannotate_write((char *)&header, sizeof(header));
	kannotate_write(str, str_size - 4);
}

EXPORT_SYMBOL(gator_annotate_marker_color_str);
