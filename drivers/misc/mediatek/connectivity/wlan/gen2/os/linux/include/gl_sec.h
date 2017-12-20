/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_sec.h#1
*/

/*! \file   p2p_fsm.h
    \brief  Declaration of functions and finite state machine for P2P Module.

    Declaration of functions and finite state machine for P2P Module.
*/

#ifndef _GL_SEC_H
#define _GL_SEC_H

extern void handle_sec_msg_1(unsigned char *msg_in, int msg_in_len, unsigned char *msg_out, int *msg_out_len);
extern void handle_sec_msg_2(unsigned char *msg_in, int msg_in_len, unsigned char *msg_out, int *msg_out_len);
extern void handle_sec_msg_3(unsigned char *msg_in, int msg_in_len, unsigned char *msg_out, int *msg_out_len);
extern void handle_sec_msg_4(unsigned char *msg_in, int msg_in_len, unsigned char *msg_out, int *msg_out_len);
extern void handle_sec_msg_5(unsigned char *msg_in, int msg_in_len, unsigned char *msg_out, int *msg_out_len);
extern void handle_sec_msg_final(unsigned char *msg_in, int msg_in_len, unsigned char *msg_out, int *msg_out_len);

#endif /* _GL_SEC_H */
