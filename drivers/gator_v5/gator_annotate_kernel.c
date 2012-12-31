/**
 * Copyright (C) ARM Limited 2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static void kannotate_write(char* ptr, unsigned int size)
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

// String annotation
void gator_annotate(char* string)
{
	kannotate_write(string, strlen(string) + 1);
}
EXPORT_SYMBOL(gator_annotate);

// String annotation with color
void gator_annotate_color(int color, char* string)
{
	kannotate_write((char*)&color, sizeof(color));
	kannotate_write(string, strlen(string) + 1);
}
EXPORT_SYMBOL(gator_annotate_color);

// Terminate an annotation
void gator_annotate_end(void)
{
	char nul = 0;
	kannotate_write(&nul, sizeof(nul));
}
EXPORT_SYMBOL(gator_annotate_end);

// Image annotation with optional string
void gator_annotate_visual(char* data, unsigned int length, char* string)
{
	long long visual_annotation = 0x011c | (strlen(string) << 16) | ((long long)length << 32);
	kannotate_write((char*)&visual_annotation, 8);
	kannotate_write(string, strlen(string));
	kannotate_write(data, length);
}
EXPORT_SYMBOL(gator_annotate_visual);

// Marker annotation
void gator_annotate_marker(void)
{
	int marker_annotation = 0x00021c;
	kannotate_write((char*)&marker_annotation, 3);
}
EXPORT_SYMBOL(gator_annotate_marker);

// Marker annotation with a string
void gator_annotate_marker_str(char* string)
{
	int marker_annotation = 0x021c;
	kannotate_write((char*)&marker_annotation, 2);
	kannotate_write(string, strlen(string) + 1);
}
EXPORT_SYMBOL(gator_annotate_marker_str);

// Marker annotation with a color
void gator_annotate_marker_color(int color)
{
	long long marker_annotation = (0x021c | ((long long)color << 16)) & 0x0000ffffffffffffLL;
	kannotate_write((char*)&marker_annotation, 7);
}
EXPORT_SYMBOL(gator_annotate_marker_color);

// Marker annotationw ith a string and color
void gator_annotate_marker_color_str(int color, char* string)
{
	long long marker_annotation = 0x021c | ((long long)color << 16);
	kannotate_write((char*)&marker_annotation, 6);
	kannotate_write(string, strlen(string) + 1);
}
EXPORT_SYMBOL(gator_annotate_marker_color_str);
