#ifndef _ONETOUCH_H_
#define _ONETOUCH_H_

#define ONETOUCH_PKT_LEN        0x02
#define ONETOUCH_BUTTON         KEY_PROG1

int onetouch_connect_input(struct us_data *ss);

#endif
