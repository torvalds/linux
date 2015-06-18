#ifndef LYNX_CURSOR_H__
#define LYNX_CURSOR_H__

/* hw_cursor_xxx works for voyager,718 and 750 */
void hw_cursor_enable(struct lynx_cursor *cursor);
void hw_cursor_disable(struct lynx_cursor *cursor);
void hw_cursor_setSize(struct lynx_cursor *cursor,
						int w, int h);
void hw_cursor_setPos(struct lynx_cursor *cursor,
						int x, int y);
void hw_cursor_setColor(struct lynx_cursor *cursor,
						u32 fg, u32 bg);
void hw_cursor_setData(struct lynx_cursor *cursor,
			u16 rop, const u8* data, const u8* mask);
void hw_cursor_setData2(struct lynx_cursor *cursor,
			u16 rop, const u8* data, const u8* mask);
#endif
