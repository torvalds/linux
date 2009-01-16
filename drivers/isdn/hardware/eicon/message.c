/*
 *
  Copyright (c) Eicon Networks, 2002.
 *
  This source file is supplied for the use with
  Eicon Networks range of DIVA Server Adapters.
 *
  Eicon File Revision :    2.1
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */





#include "platform.h"
#include "di_defs.h"
#include "pc.h"
#include "capi20.h"
#include "divacapi.h"
#include "mdm_msg.h"
#include "divasync.h"



#define FILE_ "MESSAGE.C"
#define dprintf









/*------------------------------------------------------------------*/
/* This is options supported for all adapters that are server by    */
/* XDI driver. Allo it is not necessary to ask it from every adapter*/
/* and it is not necessary to save it separate for every adapter    */
/* Macrose defined here have only local meaning                     */
/*------------------------------------------------------------------*/
static dword diva_xdi_extended_features = 0;

#define DIVA_CAPI_USE_CMA                 0x00000001
#define DIVA_CAPI_XDI_PROVIDES_SDRAM_BAR  0x00000002
#define DIVA_CAPI_XDI_PROVIDES_NO_CANCEL  0x00000004
#define DIVA_CAPI_XDI_PROVIDES_RX_DMA     0x00000008

/*
  CAPI can request to process all return codes self only if:
  protocol code supports this && xdi supports this
 */
#define DIVA_CAPI_SUPPORTS_NO_CANCEL(__a__)   (((__a__)->manufacturer_features&MANUFACTURER_FEATURE_XONOFF_FLOW_CONTROL)&&    ((__a__)->manufacturer_features & MANUFACTURER_FEATURE_OK_FC_LABEL) &&     (diva_xdi_extended_features   & DIVA_CAPI_XDI_PROVIDES_NO_CANCEL))

/*------------------------------------------------------------------*/
/* local function prototypes                                        */
/*------------------------------------------------------------------*/

static void group_optimization(DIVA_CAPI_ADAPTER   * a, PLCI   * plci);
static void set_group_ind_mask (PLCI   *plci);
static void clear_group_ind_mask_bit (PLCI   *plci, word b);
static byte test_group_ind_mask_bit (PLCI   *plci, word b);
void AutomaticLaw(DIVA_CAPI_ADAPTER   *);
word CapiRelease(word);
word CapiRegister(word);
word api_put(APPL   *, CAPI_MSG   *);
static word api_parse(byte   *, word, byte *, API_PARSE *);
static void api_save_msg(API_PARSE   *in, byte *format, API_SAVE   *out);
static void api_load_msg(API_SAVE   *in, API_PARSE   *out);

word api_remove_start(void);
void api_remove_complete(void);

static void plci_remove(PLCI   *);
static void diva_get_extended_adapter_features (DIVA_CAPI_ADAPTER  * a);
static void diva_ask_for_xdi_sdram_bar (DIVA_CAPI_ADAPTER  *, IDI_SYNC_REQ  *);

void   callback(ENTITY   *);

static void control_rc(PLCI   *, byte, byte, byte, byte, byte);
static void data_rc(PLCI   *, byte);
static void data_ack(PLCI   *, byte);
static void sig_ind(PLCI   *);
static void SendInfo(PLCI   *, dword, byte   * *, byte);
static void SendSetupInfo(APPL   *, PLCI   *, dword, byte   * *, byte);
static void SendSSExtInd(APPL   *, PLCI   * plci, dword Id, byte   * * parms);

static void VSwitchReqInd(PLCI   *plci, dword Id, byte   **parms);

static void nl_ind(PLCI   *);

static byte connect_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte connect_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte connect_a_res(dword,word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte disconnect_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte disconnect_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte listen_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte info_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte info_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte alert_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte facility_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte facility_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte connect_b3_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte connect_b3_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte connect_b3_a_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte disconnect_b3_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte disconnect_b3_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte data_b3_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte data_b3_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte reset_b3_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte reset_b3_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte connect_b3_t90_a_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte select_b_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte manufacturer_req(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
static byte manufacturer_res(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);

static word get_plci(DIVA_CAPI_ADAPTER   *);
static void add_p(PLCI   *, byte, byte   *);
static void add_s(PLCI   * plci, byte code, API_PARSE * p);
static void add_ss(PLCI   * plci, byte code, API_PARSE * p);
static void add_ie(PLCI   * plci, byte code, byte   * p, word p_length);
static void add_d(PLCI   *, word, byte   *);
static void add_ai(PLCI   *, API_PARSE *);
static word add_b1(PLCI   *, API_PARSE *, word, word);
static word add_b23(PLCI   *, API_PARSE *);
static word add_modem_b23 (PLCI  * plci, API_PARSE* bp_parms);
static void sig_req(PLCI   *, byte, byte);
static void nl_req_ncci(PLCI   *, byte, byte);
static void send_req(PLCI   *);
static void send_data(PLCI   *);
static word plci_remove_check(PLCI   *);
static void listen_check(DIVA_CAPI_ADAPTER   *);
static byte AddInfo(byte   **, byte   **, byte   *, byte *);
static byte getChannel(API_PARSE *);
static void IndParse(PLCI   *, word *, byte   **, byte);
static byte ie_compare(byte   *, byte *);
static word find_cip(DIVA_CAPI_ADAPTER   *, byte   *, byte   *);
static word CPN_filter_ok(byte   *cpn,DIVA_CAPI_ADAPTER   *,word);

/*
  XON protocol helpers
  */
static void channel_flow_control_remove (PLCI   * plci);
static void channel_x_off (PLCI   * plci, byte ch, byte flag);
static void channel_x_on (PLCI   * plci, byte ch);
static void channel_request_xon (PLCI   * plci, byte ch);
static void channel_xmit_xon (PLCI   * plci);
static int channel_can_xon (PLCI   * plci, byte ch);
static void channel_xmit_extended_xon (PLCI   * plci);

static byte SendMultiIE(PLCI   * plci, dword Id, byte   * * parms, byte ie_type, dword info_mask, byte setupParse);
static word AdvCodecSupport(DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, byte);
static void CodecIdCheck(DIVA_CAPI_ADAPTER   *, PLCI   *);
static void SetVoiceChannel(PLCI   *, byte   *, DIVA_CAPI_ADAPTER   * );
static void VoiceChannelOff(PLCI   *plci);
static void adv_voice_write_coefs (PLCI   *plci, word write_command);
static void adv_voice_clear_config (PLCI   *plci);

static word get_b1_facilities (PLCI   * plci, byte b1_resource);
static byte add_b1_facilities (PLCI   * plci, byte b1_resource, word b1_facilities);
static void adjust_b1_facilities (PLCI   *plci, byte new_b1_resource, word new_b1_facilities);
static word adjust_b_process (dword Id, PLCI   *plci, byte Rc);
static void adjust_b1_resource (dword Id, PLCI   *plci, API_SAVE   *bp_msg, word b1_facilities, word internal_command);
static void adjust_b_restore (dword Id, PLCI   *plci, byte Rc);
static void reset_b3_command (dword Id, PLCI   *plci, byte Rc);
static void select_b_command (dword Id, PLCI   *plci, byte Rc);
static void fax_connect_ack_command (dword Id, PLCI   *plci, byte Rc);
static void fax_edata_ack_command (dword Id, PLCI   *plci, byte Rc);
static void fax_connect_info_command (dword Id, PLCI   *plci, byte Rc);
static void fax_adjust_b23_command (dword Id, PLCI   *plci, byte Rc);
static void fax_disconnect_command (dword Id, PLCI   *plci, byte Rc);
static void hold_save_command (dword Id, PLCI   *plci, byte Rc);
static void retrieve_restore_command (dword Id, PLCI   *plci, byte Rc);
static void init_b1_config (PLCI   *plci);
static void clear_b1_config (PLCI   *plci);

static void dtmf_command (dword Id, PLCI   *plci, byte Rc);
static byte dtmf_request (dword Id, word Number, DIVA_CAPI_ADAPTER   *a, PLCI   *plci, APPL   *appl, API_PARSE *msg);
static void dtmf_confirmation (dword Id, PLCI   *plci);
static void dtmf_indication (dword Id, PLCI   *plci, byte   *msg, word length);
static void dtmf_parameter_write (PLCI   *plci);


static void mixer_set_bchannel_id_esc (PLCI   *plci, byte bchannel_id);
static void mixer_set_bchannel_id (PLCI   *plci, byte   *chi);
static void mixer_clear_config (PLCI   *plci);
static void mixer_notify_update (PLCI   *plci, byte others);
static void mixer_command (dword Id, PLCI   *plci, byte Rc);
static byte mixer_request (dword Id, word Number, DIVA_CAPI_ADAPTER   *a, PLCI   *plci, APPL   *appl, API_PARSE *msg);
static void mixer_indication_coefs_set (dword Id, PLCI   *plci);
static void mixer_indication_xconnect_from (dword Id, PLCI   *plci, byte   *msg, word length);
static void mixer_indication_xconnect_to (dword Id, PLCI   *plci, byte   *msg, word length);
static void mixer_remove (PLCI   *plci);


static void ec_command (dword Id, PLCI   *plci, byte Rc);
static byte ec_request (dword Id, word Number, DIVA_CAPI_ADAPTER   *a, PLCI   *plci, APPL   *appl, API_PARSE *msg);
static void ec_indication (dword Id, PLCI   *plci, byte   *msg, word length);


static void rtp_connect_b3_req_command (dword Id, PLCI   *plci, byte Rc);
static void rtp_connect_b3_res_command (dword Id, PLCI   *plci, byte Rc);


static int  diva_get_dma_descriptor  (PLCI   *plci, dword   *dma_magic);
static void diva_free_dma_descriptor (PLCI   *plci, int nr);

/*------------------------------------------------------------------*/
/* external function prototypes                                     */
/*------------------------------------------------------------------*/

extern byte MapController (byte);
extern byte UnMapController (byte);
#define MapId(Id) (((Id) & 0xffffff00L) | MapController ((byte)(Id)))
#define UnMapId(Id) (((Id) & 0xffffff00L) | UnMapController ((byte)(Id)))

void   sendf(APPL   *, word, dword, word, byte *, ...);
void   * TransmitBufferSet(APPL   * appl, dword ref);
void   * TransmitBufferGet(APPL   * appl, void   * p);
void TransmitBufferFree(APPL   * appl, void   * p);
void   * ReceiveBufferGet(APPL   * appl, int Num);

int fax_head_line_time (char *buffer);


/*------------------------------------------------------------------*/
/* Global data definitions                                          */
/*------------------------------------------------------------------*/
extern byte max_adapter;
extern byte max_appl;
extern DIVA_CAPI_ADAPTER   * adapter;
extern APPL   * application;







static byte remove_started = false;
static PLCI dummy_plci;


static struct _ftable {
  word command;
  byte * format;
  byte (* function)(dword, word, DIVA_CAPI_ADAPTER   *, PLCI   *, APPL   *, API_PARSE *);
} ftable[] = {
  {_DATA_B3_R,                          "dwww",         data_b3_req},
  {_DATA_B3_I|RESPONSE,                 "w",            data_b3_res},
  {_INFO_R,                             "ss",           info_req},
  {_INFO_I|RESPONSE,                    "",             info_res},
  {_CONNECT_R,                          "wsssssssss",   connect_req},
  {_CONNECT_I|RESPONSE,                 "wsssss",       connect_res},
  {_CONNECT_ACTIVE_I|RESPONSE,          "",             connect_a_res},
  {_DISCONNECT_R,                       "s",            disconnect_req},
  {_DISCONNECT_I|RESPONSE,              "",             disconnect_res},
  {_LISTEN_R,                           "dddss",        listen_req},
  {_ALERT_R,                            "s",            alert_req},
  {_FACILITY_R,                         "ws",           facility_req},
  {_FACILITY_I|RESPONSE,                "ws",           facility_res},
  {_CONNECT_B3_R,                       "s",            connect_b3_req},
  {_CONNECT_B3_I|RESPONSE,              "ws",           connect_b3_res},
  {_CONNECT_B3_ACTIVE_I|RESPONSE,       "",             connect_b3_a_res},
  {_DISCONNECT_B3_R,                    "s",            disconnect_b3_req},
  {_DISCONNECT_B3_I|RESPONSE,           "",             disconnect_b3_res},
  {_RESET_B3_R,                         "s",            reset_b3_req},
  {_RESET_B3_I|RESPONSE,                "",             reset_b3_res},
  {_CONNECT_B3_T90_ACTIVE_I|RESPONSE,   "ws",           connect_b3_t90_a_res},
  {_CONNECT_B3_T90_ACTIVE_I|RESPONSE,   "",             connect_b3_t90_a_res},
  {_SELECT_B_REQ,                       "s",            select_b_req},
  {_MANUFACTURER_R,                     "dws",          manufacturer_req},
  {_MANUFACTURER_I|RESPONSE,            "dws",          manufacturer_res},
  {_MANUFACTURER_I|RESPONSE,            "",             manufacturer_res}
};

static byte * cip_bc[29][2] = {
  { "",                     ""                     }, /* 0 */
  { "\x03\x80\x90\xa3",     "\x03\x80\x90\xa2"     }, /* 1 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 2 */
  { "\x02\x89\x90",         "\x02\x89\x90"         }, /* 3 */
  { "\x03\x90\x90\xa3",     "\x03\x90\x90\xa2"     }, /* 4 */
  { "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 5 */
  { "\x02\x98\x90",         "\x02\x98\x90"         }, /* 6 */
  { "\x04\x88\xc0\xc6\xe6", "\x04\x88\xc0\xc6\xe6" }, /* 7 */
  { "\x04\x88\x90\x21\x8f", "\x04\x88\x90\x21\x8f" }, /* 8 */
  { "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 9 */
  { "",                     ""                     }, /* 10 */
  { "",                     ""                     }, /* 11 */
  { "",                     ""                     }, /* 12 */
  { "",                     ""                     }, /* 13 */
  { "",                     ""                     }, /* 14 */
  { "",                     ""                     }, /* 15 */

  { "\x03\x80\x90\xa3",     "\x03\x80\x90\xa2"     }, /* 16 */
  { "\x03\x90\x90\xa3",     "\x03\x90\x90\xa2"     }, /* 17 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 18 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 19 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 20 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 21 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 22 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 23 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 24 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }, /* 25 */
  { "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 26 */
  { "\x03\x91\x90\xa5",     "\x03\x91\x90\xa5"     }, /* 27 */
  { "\x02\x88\x90",         "\x02\x88\x90"         }  /* 28 */
};

static byte * cip_hlc[29] = {
  "",                           /* 0 */
  "",                           /* 1 */
  "",                           /* 2 */
  "",                           /* 3 */
  "",                           /* 4 */
  "",                           /* 5 */
  "",                           /* 6 */
  "",                           /* 7 */
  "",                           /* 8 */
  "",                           /* 9 */
  "",                           /* 10 */
  "",                           /* 11 */
  "",                           /* 12 */
  "",                           /* 13 */
  "",                           /* 14 */
  "",                           /* 15 */

  "\x02\x91\x81",               /* 16 */
  "\x02\x91\x84",               /* 17 */
  "\x02\x91\xa1",               /* 18 */
  "\x02\x91\xa4",               /* 19 */
  "\x02\x91\xa8",               /* 20 */
  "\x02\x91\xb1",               /* 21 */
  "\x02\x91\xb2",               /* 22 */
  "\x02\x91\xb5",               /* 23 */
  "\x02\x91\xb8",               /* 24 */
  "\x02\x91\xc1",               /* 25 */
  "\x02\x91\x81",               /* 26 */
  "\x03\x91\xe0\x01",           /* 27 */
  "\x03\x91\xe0\x02"            /* 28 */
};

/*------------------------------------------------------------------*/

#define V120_HEADER_LENGTH 1
#define V120_HEADER_EXTEND_BIT  0x80
#define V120_HEADER_BREAK_BIT   0x40
#define V120_HEADER_C1_BIT      0x04
#define V120_HEADER_C2_BIT      0x08
#define V120_HEADER_FLUSH_COND  (V120_HEADER_BREAK_BIT | V120_HEADER_C1_BIT | V120_HEADER_C2_BIT)

static byte v120_default_header[] =
{

  0x83                          /*  Ext, BR , res, res, C2 , C1 , B  , F   */

};

static byte v120_break_header[] =
{

  0xc3 | V120_HEADER_BREAK_BIT  /*  Ext, BR , res, res, C2 , C1 , B  , F   */

};


/*------------------------------------------------------------------*/
/* API_PUT function                                                 */
/*------------------------------------------------------------------*/

word api_put(APPL   * appl, CAPI_MSG   * msg)
{
  word i, j, k, l, n;
  word ret;
  byte c;
  byte controller;
  DIVA_CAPI_ADAPTER   * a;
  PLCI   * plci;
  NCCI   * ncci_ptr;
  word ncci;
  CAPI_MSG   *m;
    API_PARSE msg_parms[MAX_MSG_PARMS+1];

  if (msg->header.length < sizeof (msg->header) ||
      msg->header.length > MAX_MSG_SIZE) {
    dbug(1,dprintf("bad len"));
    return _BAD_MSG;
  }

  controller = (byte)((msg->header.controller &0x7f)-1);

  /* controller starts with 0 up to (max_adapter - 1) */
  if ( controller >= max_adapter )
  {
    dbug(1,dprintf("invalid ctrl"));
    return _BAD_MSG;
  }
  
  a = &adapter[controller];
  plci = NULL;
  if ((msg->header.plci != 0) && (msg->header.plci <= a->max_plci) && !a->adapter_disabled)
  {
    dbug(1,dprintf("plci=%x",msg->header.plci));
    plci = &a->plci[msg->header.plci-1];
    ncci = GET_WORD(&msg->header.ncci);
    if (plci->Id
     && (plci->appl
      || (plci->State == INC_CON_PENDING)
      || (plci->State == INC_CON_ALERT)
      || (msg->header.command == (_DISCONNECT_I|RESPONSE)))
     && ((ncci == 0)
      || (msg->header.command == (_DISCONNECT_B3_I|RESPONSE))
      || ((ncci < MAX_NCCI+1) && (a->ncci_plci[ncci] == plci->Id))))
    {
      i = plci->msg_in_read_pos;
      j = plci->msg_in_write_pos;
      if (j >= i)
      {
        if (j + msg->header.length + MSG_IN_OVERHEAD <= MSG_IN_QUEUE_SIZE)
          i += MSG_IN_QUEUE_SIZE - j;
        else
          j = 0;
      }
      else
      {

        n = (((CAPI_MSG   *)(plci->msg_in_queue))->header.length + MSG_IN_OVERHEAD + 3) & 0xfffc;

        if (i > MSG_IN_QUEUE_SIZE - n)
          i = MSG_IN_QUEUE_SIZE - n + 1;
        i -= j;
      }

      if (i <= ((msg->header.length + MSG_IN_OVERHEAD + 3) & 0xfffc))

      {
        dbug(0,dprintf("Q-FULL1(msg) - len=%d write=%d read=%d wrap=%d free=%d",
          msg->header.length, plci->msg_in_write_pos,
          plci->msg_in_read_pos, plci->msg_in_wrap_pos, i));

        return _QUEUE_FULL;
      }
      c = false;
      if ((((byte   *) msg) < ((byte   *)(plci->msg_in_queue)))
       || (((byte   *) msg) >= ((byte   *)(plci->msg_in_queue)) + sizeof(plci->msg_in_queue)))
      {
        if (plci->msg_in_write_pos != plci->msg_in_read_pos)
          c = true;
      }
      if (msg->header.command == _DATA_B3_R)
      {
        if (msg->header.length < 20)
        {
          dbug(1,dprintf("DATA_B3 REQ wrong length %d", msg->header.length));
          return _BAD_MSG;
        }
        ncci_ptr = &(a->ncci[ncci]);
        n = ncci_ptr->data_pending;
        l = ncci_ptr->data_ack_pending;
        k = plci->msg_in_read_pos;
        while (k != plci->msg_in_write_pos)
        {
          if (k == plci->msg_in_wrap_pos)
            k = 0;
          if ((((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[k]))->header.command == _DATA_B3_R)
           && (((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[k]))->header.ncci == ncci))
          {
            n++;
            if (((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[k]))->info.data_b3_req.Flags & 0x0004)
              l++;
          }

          k += (((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[k]))->header.length +
            MSG_IN_OVERHEAD + 3) & 0xfffc;

        }
        if ((n >= MAX_DATA_B3) || (l >= MAX_DATA_ACK))
        {
          dbug(0,dprintf("Q-FULL2(data) - pending=%d/%d ack_pending=%d/%d",
                          ncci_ptr->data_pending, n, ncci_ptr->data_ack_pending, l));

          return _QUEUE_FULL;
        }
        if (plci->req_in || plci->internal_command)
        {
          if ((((byte   *) msg) >= ((byte   *)(plci->msg_in_queue)))
           && (((byte   *) msg) < ((byte   *)(plci->msg_in_queue)) + sizeof(plci->msg_in_queue)))
          {
            dbug(0,dprintf("Q-FULL3(requeue)"));

            return _QUEUE_FULL;
          }
          c = true;
        }
      }
      else
      {
        if (plci->req_in || plci->internal_command)
          c = true;
        else
        {
          plci->command = msg->header.command;
          plci->number = msg->header.number;
        }
      }
      if (c)
      {
        dbug(1,dprintf("enqueue msg(0x%04x,0x%x,0x%x) - len=%d write=%d read=%d wrap=%d free=%d",
          msg->header.command, plci->req_in, plci->internal_command,
          msg->header.length, plci->msg_in_write_pos,
          plci->msg_in_read_pos, plci->msg_in_wrap_pos, i));
        if (j == 0)
          plci->msg_in_wrap_pos = plci->msg_in_write_pos;
        m = (CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[j]);
        for (i = 0; i < msg->header.length; i++)
          ((byte   *)(plci->msg_in_queue))[j++] = ((byte   *) msg)[i];
        if (m->header.command == _DATA_B3_R)
        {

          m->info.data_b3_req.Data = (dword)(long)(TransmitBufferSet (appl, m->info.data_b3_req.Data));

        }

        j = (j + 3) & 0xfffc;

        *((APPL   *   *)(&((byte   *)(plci->msg_in_queue))[j])) = appl;
        plci->msg_in_write_pos = j + MSG_IN_OVERHEAD;
        return 0;
      }
    }
    else
    {
      plci = NULL;
    }
  }
  dbug(1,dprintf("com=%x",msg->header.command));

  for(j=0;j<MAX_MSG_PARMS+1;j++) msg_parms[j].length = 0;
  for(i=0, ret = _BAD_MSG;
      i<(sizeof(ftable)/sizeof(struct _ftable));
      i++) {

    if(ftable[i].command==msg->header.command) {
      /* break loop if the message is correct, otherwise continue scan  */
      /* (for example: CONNECT_B3_T90_ACT_RES has two specifications)   */
      if(!api_parse(msg->info.b,(word)(msg->header.length-12),ftable[i].format,msg_parms)) {
        ret = 0;
        break;
      }
      for(j=0;j<MAX_MSG_PARMS+1;j++) msg_parms[j].length = 0;
    }
  }
  if(ret) {
    dbug(1,dprintf("BAD_MSG"));
    if(plci) plci->command = 0;
    return ret;
  }


  c = ftable[i].function(GET_DWORD(&msg->header.controller),
                         msg->header.number,
                         a,
                         plci,
                         appl,
                         msg_parms);

  channel_xmit_extended_xon (plci);

  if(c==1) send_req(plci);
  if(c==2 && plci) plci->req_in = plci->req_in_start = plci->req_out = 0;
  if(plci && !plci->req_in) plci->command = 0;
  return 0;
}


/*------------------------------------------------------------------*/
/* api_parse function, check the format of api messages             */
/*------------------------------------------------------------------*/

static word api_parse(byte *msg, word length, byte *format, API_PARSE *parms)
{
  word i;
  word p;

  for(i=0,p=0; format[i]; i++) {
    if(parms)
    {
      parms[i].info = &msg[p];
    }
    switch(format[i]) {
    case 'b':
      p +=1;
      break;
    case 'w':
      p +=2;
      break;
    case 'd':
      p +=4;
      break;
    case 's':
      if(msg[p]==0xff) {
        parms[i].info +=2;
        parms[i].length = msg[p+1] + (msg[p+2]<<8);
        p +=(parms[i].length +3);
      }
      else {
        parms[i].length = msg[p];
        p +=(parms[i].length +1);
      }
      break;
    }

    if(p>length) return true;
  }
  if(parms) parms[i].info = NULL;
  return false;
}

static void api_save_msg(API_PARSE *in, byte *format, API_SAVE *out)
{
  word i, j, n = 0;
  byte   *p;

  p = out->info;
  for (i = 0; format[i] != '\0'; i++)
  {
    out->parms[i].info = p;
    out->parms[i].length = in[i].length;
    switch (format[i])
    {
    case 'b':
      n = 1;
      break;
    case 'w':
      n = 2;
      break;
    case 'd':
      n = 4;
      break;
    case 's':
      n = in[i].length + 1;
      break;
    }
    for (j = 0; j < n; j++)
      *(p++) = in[i].info[j];
  }
  out->parms[i].info = NULL;
  out->parms[i].length = 0;
}

static void api_load_msg(API_SAVE *in, API_PARSE *out)
{
  word i;

  i = 0;
  do
  {
    out[i].info = in->parms[i].info;
    out[i].length = in->parms[i].length;
  } while (in->parms[i++].info);
}


/*------------------------------------------------------------------*/
/* CAPI remove function                                             */
/*------------------------------------------------------------------*/

word api_remove_start(void)
{
  word i;
  word j;

  if(!remove_started) {
    remove_started = true;
    for(i=0;i<max_adapter;i++) {
      if(adapter[i].request) {
        for(j=0;j<adapter[i].max_plci;j++) {
          if(adapter[i].plci[j].Sig.Id) plci_remove(&adapter[i].plci[j]);
        }
      }
    }
    return 1;
  }
  else {
    for(i=0;i<max_adapter;i++) {
      if(adapter[i].request) {
        for(j=0;j<adapter[i].max_plci;j++) {
          if(adapter[i].plci[j].Sig.Id) return 1;
        }
      }
    }
  }
  api_remove_complete();
  return 0;
}


/*------------------------------------------------------------------*/
/* internal command queue                                           */
/*------------------------------------------------------------------*/

static void init_internal_command_queue (PLCI   *plci)
{
  word i;

  dbug (1, dprintf ("%s,%d: init_internal_command_queue",
    (char   *)(FILE_), __LINE__));

  plci->internal_command = 0;
  for (i = 0; i < MAX_INTERNAL_COMMAND_LEVELS; i++)
    plci->internal_command_queue[i] = NULL;
}


static void start_internal_command (dword Id, PLCI   *plci, t_std_internal_command command_function)
{
  word i;

  dbug (1, dprintf ("[%06lx] %s,%d: start_internal_command",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  if (plci->internal_command == 0)
  {
    plci->internal_command_queue[0] = command_function;
    (* command_function)(Id, plci, OK);
  }
  else
  {
    i = 1;
    while (plci->internal_command_queue[i] != NULL)
      i++;
    plci->internal_command_queue[i] = command_function;
  }
}


static void next_internal_command (dword Id, PLCI   *plci)
{
  word i;

  dbug (1, dprintf ("[%06lx] %s,%d: next_internal_command",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  plci->internal_command = 0;
  plci->internal_command_queue[0] = NULL;
  while (plci->internal_command_queue[1] != NULL)
  {
    for (i = 0; i < MAX_INTERNAL_COMMAND_LEVELS - 1; i++)
      plci->internal_command_queue[i] = plci->internal_command_queue[i+1];
    plci->internal_command_queue[MAX_INTERNAL_COMMAND_LEVELS - 1] = NULL;
    (*(plci->internal_command_queue[0]))(Id, plci, OK);
    if (plci->internal_command != 0)
      return;
    plci->internal_command_queue[0] = NULL;
  }
}


/*------------------------------------------------------------------*/
/* NCCI allocate/remove function                                    */
/*------------------------------------------------------------------*/

static dword ncci_mapping_bug = 0;

static word get_ncci (PLCI   *plci, byte ch, word force_ncci)
{
  DIVA_CAPI_ADAPTER   *a;
  word ncci, i, j, k;

  a = plci->adapter;
  if (!ch || a->ch_ncci[ch])
  {
    ncci_mapping_bug++;
    dbug(1,dprintf("NCCI mapping exists %ld %02x %02x %02x-%02x",
      ncci_mapping_bug, ch, force_ncci, a->ncci_ch[a->ch_ncci[ch]], a->ch_ncci[ch]));
    ncci = ch;
  }
  else
  {
    if (force_ncci)
      ncci = force_ncci;
    else
    {
      if ((ch < MAX_NCCI+1) && !a->ncci_ch[ch])
        ncci = ch;
      else
      {
        ncci = 1;
        while ((ncci < MAX_NCCI+1) && a->ncci_ch[ncci])
          ncci++;
        if (ncci == MAX_NCCI+1)
        {
          ncci_mapping_bug++;
          i = 1;
          do
          {
            j = 1;
            while ((j < MAX_NCCI+1) && (a->ncci_ch[j] != i))
              j++;
            k = j;
            if (j < MAX_NCCI+1)
            {
              do
              {
                j++;
              } while ((j < MAX_NCCI+1) && (a->ncci_ch[j] != i));
            }
          } while ((i < MAX_NL_CHANNEL+1) && (j < MAX_NCCI+1));
          if (i < MAX_NL_CHANNEL+1)
          {
            dbug(1,dprintf("NCCI mapping overflow %ld %02x %02x %02x-%02x-%02x",
              ncci_mapping_bug, ch, force_ncci, i, k, j));
          }
          else
          {
            dbug(1,dprintf("NCCI mapping overflow %ld %02x %02x",
              ncci_mapping_bug, ch, force_ncci));
          }
          ncci = ch;
        }
      }
      a->ncci_plci[ncci] = plci->Id;
      a->ncci_state[ncci] = IDLE;
      if (!plci->ncci_ring_list)
        plci->ncci_ring_list = ncci;
      else
        a->ncci_next[ncci] = a->ncci_next[plci->ncci_ring_list];
      a->ncci_next[plci->ncci_ring_list] = (byte) ncci;
    }
    a->ncci_ch[ncci] = ch;
    a->ch_ncci[ch] = (byte) ncci;
    dbug(1,dprintf("NCCI mapping established %ld %02x %02x %02x-%02x",
      ncci_mapping_bug, ch, force_ncci, ch, ncci));
  }
  return (ncci);
}


static void ncci_free_receive_buffers (PLCI   *plci, word ncci)
{
  DIVA_CAPI_ADAPTER   *a;
  APPL   *appl;
  word i, ncci_code;
  dword Id;

  a = plci->adapter;
  Id = (((dword) ncci) << 16) | (((word)(plci->Id)) << 8) | a->Id;
  if (ncci)
  {
    if (a->ncci_plci[ncci] == plci->Id)
    {
      if (!plci->appl)
      {
        ncci_mapping_bug++;
        dbug(1,dprintf("NCCI mapping appl expected %ld %08lx",
          ncci_mapping_bug, Id));
      }
      else
      {
        appl = plci->appl;
        ncci_code = ncci | (((word) a->Id) << 8);
        for (i = 0; i < appl->MaxBuffer; i++)
        {
          if ((appl->DataNCCI[i] == ncci_code)
           && (((byte)(appl->DataFlags[i] >> 8)) == plci->Id))
          {
            appl->DataNCCI[i] = 0;
          }
        }
      }
    }
  }
  else
  {
    for (ncci = 1; ncci < MAX_NCCI+1; ncci++)
    {
      if (a->ncci_plci[ncci] == plci->Id)
      {
        if (!plci->appl)
        {
          ncci_mapping_bug++;
          dbug(1,dprintf("NCCI mapping no appl %ld %08lx",
            ncci_mapping_bug, Id));
        }
        else
        {
          appl = plci->appl;
          ncci_code = ncci | (((word) a->Id) << 8);
          for (i = 0; i < appl->MaxBuffer; i++)
          {
            if ((appl->DataNCCI[i] == ncci_code)
             && (((byte)(appl->DataFlags[i] >> 8)) == plci->Id))
            {
              appl->DataNCCI[i] = 0;
            }
          }
        }
      }
    }
  }
}


static void cleanup_ncci_data (PLCI   *plci, word ncci)
{
  NCCI   *ncci_ptr;

  if (ncci && (plci->adapter->ncci_plci[ncci] == plci->Id))
  {
    ncci_ptr = &(plci->adapter->ncci[ncci]);
    if (plci->appl)
    {
      while (ncci_ptr->data_pending != 0)
      {
        if (!plci->data_sent || (ncci_ptr->DBuffer[ncci_ptr->data_out].P != plci->data_sent_ptr))
          TransmitBufferFree (plci->appl, ncci_ptr->DBuffer[ncci_ptr->data_out].P);
        (ncci_ptr->data_out)++;
        if (ncci_ptr->data_out == MAX_DATA_B3)
          ncci_ptr->data_out = 0;
        (ncci_ptr->data_pending)--;
      }
    }
    ncci_ptr->data_out = 0;
    ncci_ptr->data_pending = 0;
    ncci_ptr->data_ack_out = 0;
    ncci_ptr->data_ack_pending = 0;
  }
}


static void ncci_remove (PLCI   *plci, word ncci, byte preserve_ncci)
{
  DIVA_CAPI_ADAPTER   *a;
  dword Id;
  word i;

  a = plci->adapter;
  Id = (((dword) ncci) << 16) | (((word)(plci->Id)) << 8) | a->Id;
  if (!preserve_ncci)
    ncci_free_receive_buffers (plci, ncci);
  if (ncci)
  {
    if (a->ncci_plci[ncci] != plci->Id)
    {
      ncci_mapping_bug++;
      dbug(1,dprintf("NCCI mapping doesn't exist %ld %08lx %02x",
        ncci_mapping_bug, Id, preserve_ncci));
    }
    else
    {
      cleanup_ncci_data (plci, ncci);
      dbug(1,dprintf("NCCI mapping released %ld %08lx %02x %02x-%02x",
        ncci_mapping_bug, Id, preserve_ncci, a->ncci_ch[ncci], ncci));
      a->ch_ncci[a->ncci_ch[ncci]] = 0;
      if (!preserve_ncci)
      {
        a->ncci_ch[ncci] = 0;
        a->ncci_plci[ncci] = 0;
        a->ncci_state[ncci] = IDLE;
        i = plci->ncci_ring_list;
        while ((i != 0) && (a->ncci_next[i] != plci->ncci_ring_list) && (a->ncci_next[i] != ncci))
          i = a->ncci_next[i];
        if ((i != 0) && (a->ncci_next[i] == ncci))
        {
          if (i == ncci)
            plci->ncci_ring_list = 0;
          else if (plci->ncci_ring_list == ncci)
            plci->ncci_ring_list = i;
          a->ncci_next[i] = a->ncci_next[ncci];
        }
        a->ncci_next[ncci] = 0;
      }
    }
  }
  else
  {
    for (ncci = 1; ncci < MAX_NCCI+1; ncci++)
    {
      if (a->ncci_plci[ncci] == plci->Id)
      {
        cleanup_ncci_data (plci, ncci);
        dbug(1,dprintf("NCCI mapping released %ld %08lx %02x %02x-%02x",
          ncci_mapping_bug, Id, preserve_ncci, a->ncci_ch[ncci], ncci));
        a->ch_ncci[a->ncci_ch[ncci]] = 0;
        if (!preserve_ncci)
        {
          a->ncci_ch[ncci] = 0;
          a->ncci_plci[ncci] = 0;
          a->ncci_state[ncci] = IDLE;
          a->ncci_next[ncci] = 0;
        }
      }
    }
    if (!preserve_ncci)
      plci->ncci_ring_list = 0;
  }
}


/*------------------------------------------------------------------*/
/* PLCI remove function                                             */
/*------------------------------------------------------------------*/

static void plci_free_msg_in_queue (PLCI   *plci)
{
  word i;

  if (plci->appl)
  {
    i = plci->msg_in_read_pos;
    while (i != plci->msg_in_write_pos)
    {
      if (i == plci->msg_in_wrap_pos)
        i = 0;
      if (((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[i]))->header.command == _DATA_B3_R)
      {

        TransmitBufferFree (plci->appl,
          (byte *)(long)(((CAPI_MSG *)(&((byte *)(plci->msg_in_queue))[i]))->info.data_b3_req.Data));

      }

      i += (((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[i]))->header.length +
        MSG_IN_OVERHEAD + 3) & 0xfffc;

    }
  }
  plci->msg_in_write_pos = MSG_IN_QUEUE_SIZE;
  plci->msg_in_read_pos = MSG_IN_QUEUE_SIZE;
  plci->msg_in_wrap_pos = MSG_IN_QUEUE_SIZE;
}


static void plci_remove(PLCI   * plci)
{

  if(!plci) {
    dbug(1,dprintf("plci_remove(no plci)"));
    return;
  }
  init_internal_command_queue (plci);
  dbug(1,dprintf("plci_remove(%x,tel=%x)",plci->Id,plci->tel));
  if(plci_remove_check(plci))
  {
    return;
  }
  if (plci->Sig.Id == 0xff)
  {
    dbug(1,dprintf("D-channel X.25 plci->NL.Id:%0x", plci->NL.Id));
    if (plci->NL.Id && !plci->nl_remove_id)
    {
      nl_req_ncci(plci,REMOVE,0);
      send_req(plci);
    }
  }
  else
  {
    if (!plci->sig_remove_id
     && (plci->Sig.Id
      || (plci->req_in!=plci->req_out)
      || (plci->nl_req || plci->sig_req)))
    {
      sig_req(plci,HANGUP,0);
      send_req(plci);
    }
  }
  ncci_remove (plci, 0, false);
  plci_free_msg_in_queue (plci);

  plci->channels = 0;
  plci->appl = NULL;
  if ((plci->State == INC_CON_PENDING) || (plci->State == INC_CON_ALERT))
    plci->State = OUTG_DIS_PENDING;
}

/*------------------------------------------------------------------*/
/* Application Group function helpers                               */
/*------------------------------------------------------------------*/

static void set_group_ind_mask (PLCI   *plci)
{
  word i;

  for (i = 0; i < C_IND_MASK_DWORDS; i++)
    plci->group_optimization_mask_table[i] = 0xffffffffL;
}

static void clear_group_ind_mask_bit (PLCI   *plci, word b)
{
  plci->group_optimization_mask_table[b >> 5] &= ~(1L << (b & 0x1f));
}

static byte test_group_ind_mask_bit (PLCI   *plci, word b)
{
  return ((plci->group_optimization_mask_table[b >> 5] & (1L << (b & 0x1f))) != 0);
}

/*------------------------------------------------------------------*/
/* c_ind_mask operations for arbitrary MAX_APPL                     */
/*------------------------------------------------------------------*/

static void clear_c_ind_mask (PLCI   *plci)
{
  word i;

  for (i = 0; i < C_IND_MASK_DWORDS; i++)
    plci->c_ind_mask_table[i] = 0;
}

static byte c_ind_mask_empty (PLCI   *plci)
{
  word i;

  i = 0;
  while ((i < C_IND_MASK_DWORDS) && (plci->c_ind_mask_table[i] == 0))
    i++;
  return (i == C_IND_MASK_DWORDS);
}

static void set_c_ind_mask_bit (PLCI   *plci, word b)
{
  plci->c_ind_mask_table[b >> 5] |= (1L << (b & 0x1f));
}

static void clear_c_ind_mask_bit (PLCI   *plci, word b)
{
  plci->c_ind_mask_table[b >> 5] &= ~(1L << (b & 0x1f));
}

static byte test_c_ind_mask_bit (PLCI   *plci, word b)
{
  return ((plci->c_ind_mask_table[b >> 5] & (1L << (b & 0x1f))) != 0);
}

static void dump_c_ind_mask (PLCI   *plci)
{
static char hex_digit_table[0x10] =
  {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
  word i, j, k;
  dword d;
    char *p;
    char buf[40];

  for (i = 0; i < C_IND_MASK_DWORDS; i += 4)
  {
    p = buf + 36;
    *p = '\0';
    for (j = 0; j < 4; j++)
    {
      if (i+j < C_IND_MASK_DWORDS)
      {
        d = plci->c_ind_mask_table[i+j];
        for (k = 0; k < 8; k++)
        {
          *(--p) = hex_digit_table[d & 0xf];
          d >>= 4;
        }
      }
      else if (i != 0)
      {
        for (k = 0; k < 8; k++)
          *(--p) = ' ';
      }
      *(--p) = ' ';
    }
    dbug(1,dprintf ("c_ind_mask =%s", (char   *) p));
  }
}





#define dump_plcis(a)



/*------------------------------------------------------------------*/
/* translation function for each message                            */
/*------------------------------------------------------------------*/

byte connect_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word ch;
  word i;
  word Info;
  word CIP;
  byte LinkLayer;
  API_PARSE * ai;
  API_PARSE * bp;
    API_PARSE ai_parms[5];
  word channel = 0;
  dword ch_mask;
  byte m;
  static byte esc_chi[35] = {0x02,0x18,0x01};
  static byte lli[2] = {0x01,0x00};
  byte noCh = 0;
  word dir = 0;
  byte   *p_chi = "";

  for(i=0;i<5;i++) ai_parms[i].length = 0;

  dbug(1,dprintf("connect_req(%d)",parms->length));
  Info = _WRONG_IDENTIFIER;
  if(a)
  {
    if(a->adapter_disabled)
    {
      dbug(1,dprintf("adapter disabled"));
      Id = ((word)1<<8)|a->Id;
      sendf(appl,_CONNECT_R|CONFIRM,Id,Number,"w",0);
      sendf(appl, _DISCONNECT_I, Id, 0, "w", _L1_ERROR);
      return false;
    }
    Info = _OUT_OF_PLCI;
    if((i=get_plci(a)))
    {
      Info = 0;
      plci = &a->plci[i-1];
      plci->appl = appl;
      plci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
      /* check 'external controller' bit for codec support */
      if(Id & EXT_CONTROLLER)
      {
        if(AdvCodecSupport(a, plci, appl, 0) )
        {
          plci->Id = 0;
          sendf(appl, _CONNECT_R|CONFIRM, Id, Number, "w", _WRONG_IDENTIFIER);
          return 2;
        }
      }
      ai = &parms[9];
      bp = &parms[5];
      ch = 0;
      if(bp->length)LinkLayer = bp->info[3];
      else LinkLayer = 0;
      if(ai->length)
      {
        ch=0xffff;
        if(!api_parse(&ai->info[1],(word)ai->length,"ssss",ai_parms))
        {
          ch = 0;
          if(ai_parms[0].length)
          {
            ch = GET_WORD(ai_parms[0].info+1);
            if(ch>4) ch=0; /* safety -> ignore ChannelID */
            if(ch==4) /* explizit CHI in message */
            {
              /* check length of B-CH struct */
              if((ai_parms[0].info)[3]>=1)
              {
                if((ai_parms[0].info)[4]==CHI)
                {
                  p_chi = &((ai_parms[0].info)[5]);
                }
                else
                {
                  p_chi = &((ai_parms[0].info)[3]);
                }
                if(p_chi[0]>35) /* check length of channel ID */
                {
                  Info = _WRONG_MESSAGE_FORMAT;    
                }
              }
              else Info = _WRONG_MESSAGE_FORMAT;    
            }

            if(ch==3 && ai_parms[0].length>=7 && ai_parms[0].length<=36)
            {
              dir = GET_WORD(ai_parms[0].info+3);
              ch_mask = 0;
              m = 0x3f;
              for(i=0; i+5<=ai_parms[0].length; i++)
              {
                if(ai_parms[0].info[i+5]!=0)
                {
                  if((ai_parms[0].info[i+5] | m) != 0xff)
                    Info = _WRONG_MESSAGE_FORMAT;
                  else
                  {
                    if (ch_mask == 0)
                      channel = i;
                    ch_mask |= 1L << i;
                  }
                }
                m = 0;
              }
              if (ch_mask == 0)
                Info = _WRONG_MESSAGE_FORMAT;
              if (!Info)
              {
                if ((ai_parms[0].length == 36) || (ch_mask != ((dword)(1L << channel))))
                {
                  esc_chi[0] = (byte)(ai_parms[0].length - 2);
                  for(i=0; i+5<=ai_parms[0].length; i++)
                    esc_chi[i+3] = ai_parms[0].info[i+5];
                }
                else
                  esc_chi[0] = 2;
                esc_chi[2] = (byte)channel;
                plci->b_channel = (byte)channel; /* not correct for ETSI ch 17..31 */
                add_p(plci,LLI,lli);
                add_p(plci,ESC,esc_chi);
                plci->State = LOCAL_CONNECT;
                if(!dir) plci->call_dir |= CALL_DIR_FORCE_OUTG_NL;     /* dir 0=DTE, 1=DCE */
              }
            }
          }
        }
        else  Info = _WRONG_MESSAGE_FORMAT;
      }

      dbug(1,dprintf("ch=%x,dir=%x,p_ch=%d",ch,dir,channel));
      plci->command = _CONNECT_R;
      plci->number = Number;
      /* x.31 or D-ch free SAPI in LinkLayer? */
      if(ch==1 && LinkLayer!=3 && LinkLayer!=12) noCh = true;
      if((ch==0 || ch==2 || noCh || ch==3 || ch==4) && !Info)
      {
        /* B-channel used for B3 connections (ch==0), or no B channel    */
        /* is used (ch==2) or perm. connection (3) is used  do a CALL    */
        if(noCh) Info = add_b1(plci,&parms[5],2,0);    /* no resource    */
        else     Info = add_b1(plci,&parms[5],ch,0); 
        add_s(plci,OAD,&parms[2]);
        add_s(plci,OSA,&parms[4]);
        add_s(plci,BC,&parms[6]);
        add_s(plci,LLC,&parms[7]);
        add_s(plci,HLC,&parms[8]);
        CIP = GET_WORD(parms[0].info);
        if (a->Info_Mask[appl->Id-1] & 0x200)
        {
          /* early B3 connect (CIP mask bit 9) no release after a disc */
          add_p(plci,LLI,"\x01\x01");
        }
        if(GET_WORD(parms[0].info)<29) {
          add_p(plci,BC,cip_bc[GET_WORD(parms[0].info)][a->u_law]);
          add_p(plci,HLC,cip_hlc[GET_WORD(parms[0].info)]);
        }
        add_p(plci,UID,"\x06\x43\x61\x70\x69\x32\x30");
        sig_req(plci,ASSIGN,DSIG_ID);
      }
      else if(ch==1) {

        /* D-Channel used for B3 connections */
        plci->Sig.Id = 0xff;
        Info = 0;
      }

      if(!Info && ch!=2 && !noCh ) {
        Info = add_b23(plci,&parms[5]);
        if(!Info) {
          if(!(plci->tel && !plci->adv_nl))nl_req_ncci(plci,ASSIGN,0);
        }
      }

      if(!Info)
      {
        if(ch==0 || ch==2 || ch==3 || noCh || ch==4)
        {
          if(plci->spoofed_msg==SPOOFING_REQUIRED)
          {
            api_save_msg(parms, "wsssssssss", &plci->saved_msg);
            plci->spoofed_msg = CALL_REQ;
            plci->internal_command = BLOCK_PLCI;
            plci->command = 0;
            dbug(1,dprintf("Spoof"));
            send_req(plci);
            return false;
          }
          if(ch==4)add_p(plci,CHI,p_chi);
          add_s(plci,CPN,&parms[1]);
          add_s(plci,DSA,&parms[3]);
          if(noCh) add_p(plci,ESC,"\x02\x18\xfd");  /* D-channel, no B-L3 */
          add_ai(plci,&parms[9]);
          if(!dir)sig_req(plci,CALL_REQ,0);
          else
          {
            plci->command = PERM_LIST_REQ;
            plci->appl = appl;
            sig_req(plci,LISTEN_REQ,0);
            send_req(plci);
            return false;
          }
        }
        send_req(plci);
        return false;
      }
      plci->Id = 0;
    }
  }
  sendf(appl,
        _CONNECT_R|CONFIRM,
        Id,
        Number,
        "w",Info);
  return 2;
}

byte connect_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word i, Info;
  word Reject;
  static byte cau_t[] = {0,0,0x90,0x91,0xac,0x9d,0x86,0xd8,0x9b};
  static byte esc_t[] = {0x03,0x08,0x00,0x00};
  API_PARSE * ai;
    API_PARSE ai_parms[5];
  word ch=0;

  if(!plci) {
    dbug(1,dprintf("connect_res(no plci)"));
    return 0;  /* no plci, no send */
  }

  dbug(1,dprintf("connect_res(State=0x%x)",plci->State));
  for(i=0;i<5;i++) ai_parms[i].length = 0;
  ai = &parms[5];
  dbug(1,dprintf("ai->length=%d",ai->length));

  if(ai->length)
  {
    if(!api_parse(&ai->info[1],(word)ai->length,"ssss",ai_parms))
    {
      dbug(1,dprintf("ai_parms[0].length=%d/0x%x",ai_parms[0].length,GET_WORD(ai_parms[0].info+1)));
      ch = 0;
      if(ai_parms[0].length)
      {
        ch = GET_WORD(ai_parms[0].info+1);
        dbug(1,dprintf("BCH-I=0x%x",ch));
      }
    }
  }

  if(plci->State==INC_CON_CONNECTED_ALERT)
  {
    dbug(1,dprintf("Connected Alert Call_Res"));
    if (a->Info_Mask[appl->Id-1] & 0x200)
    {
    /* early B3 connect (CIP mask bit 9) no release after a disc */
      add_p(plci,LLI,"\x01\x01");
    }
    add_s(plci, CONN_NR, &parms[2]);
    add_s(plci, LLC, &parms[4]);
    add_ai(plci, &parms[5]);
    plci->State = INC_CON_ACCEPT;
    sig_req(plci, CALL_RES,0);
    return 1;
  }
  else if(plci->State==INC_CON_PENDING || plci->State==INC_CON_ALERT) {
    clear_c_ind_mask_bit (plci, (word)(appl->Id-1));
    dump_c_ind_mask (plci);
    Reject = GET_WORD(parms[0].info);
    dbug(1,dprintf("Reject=0x%x",Reject));
    if(Reject) 
    {
      if(c_ind_mask_empty (plci)) 
      {
        if((Reject&0xff00)==0x3400) 
        {
          esc_t[2] = ((byte)(Reject&0x00ff)) | 0x80;
          add_p(plci,ESC,esc_t);
          add_ai(plci, &parms[5]);
          sig_req(plci,REJECT,0);
        }      
        else if(Reject==1 || Reject>9) 
        {
          add_ai(plci, &parms[5]);
          sig_req(plci,HANGUP,0);
        }
        else 
        {
          esc_t[2] = cau_t[(Reject&0x000f)];
          add_p(plci,ESC,esc_t);
          add_ai(plci, &parms[5]);
          sig_req(plci,REJECT,0);
        }
        plci->appl = appl;
      }
      else 
      {
        sendf(appl, _DISCONNECT_I, Id, 0, "w", _OTHER_APPL_CONNECTED);
      }
    }
    else {
      plci->appl = appl;
      if(Id & EXT_CONTROLLER){
        if(AdvCodecSupport(a, plci, appl, 0)){
          dbug(1,dprintf("connect_res(error from AdvCodecSupport)"));
          sig_req(plci,HANGUP,0);
          return 1;
        }
        if(plci->tel == ADV_VOICE && a->AdvCodecPLCI)
        {
          Info = add_b23(plci, &parms[1]);
          if (Info)
          {
            dbug(1,dprintf("connect_res(error from add_b23)"));
            sig_req(plci,HANGUP,0);
            return 1;
          }
          if(plci->adv_nl)
          {
            nl_req_ncci(plci, ASSIGN, 0);
          }
        }
      }
      else
      {
        plci->tel = 0;
        if(ch!=2)
        {
          Info = add_b23(plci, &parms[1]);
          if (Info)
          {
            dbug(1,dprintf("connect_res(error from add_b23 2)"));
            sig_req(plci,HANGUP,0);
            return 1;
          }
        }
        nl_req_ncci(plci, ASSIGN, 0);
      }

      if(plci->spoofed_msg==SPOOFING_REQUIRED)
      {
        api_save_msg(parms, "wsssss", &plci->saved_msg);
        plci->spoofed_msg = CALL_RES;
        plci->internal_command = BLOCK_PLCI;
        plci->command = 0;
        dbug(1,dprintf("Spoof"));
      }
      else
      {
        add_b1 (plci, &parms[1], ch, plci->B1_facilities);
        if (a->Info_Mask[appl->Id-1] & 0x200)
        {
          /* early B3 connect (CIP mask bit 9) no release after a disc */
          add_p(plci,LLI,"\x01\x01");
        }
        add_s(plci, CONN_NR, &parms[2]);
        add_s(plci, LLC, &parms[4]);
        add_ai(plci, &parms[5]);
        plci->State = INC_CON_ACCEPT;
        sig_req(plci, CALL_RES,0);
      }

      for(i=0; i<max_appl; i++) {
        if(test_c_ind_mask_bit (plci, i)) {
          sendf(&application[i], _DISCONNECT_I, Id, 0, "w", _OTHER_APPL_CONNECTED);
        }
      }
    }
  }
  return 1;
}

byte connect_a_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  dbug(1,dprintf("connect_a_res"));
  return false;
}

byte disconnect_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  word Info;
  word i;

  dbug(1,dprintf("disconnect_req"));

  Info = _WRONG_IDENTIFIER;

  if(plci)
  {
    if(plci->State==INC_CON_PENDING || plci->State==INC_CON_ALERT)
    {
      clear_c_ind_mask_bit (plci, (word)(appl->Id-1));
      plci->appl = appl;
      for(i=0; i<max_appl; i++)
      {
        if(test_c_ind_mask_bit (plci, i))
          sendf(&application[i], _DISCONNECT_I, Id, 0, "w", 0);
      }
      plci->State = OUTG_DIS_PENDING;
    }
    if(plci->Sig.Id && plci->appl)
    {
      Info = 0;
        if(plci->Sig.Id!=0xff)
        {
          if(plci->State!=INC_DIS_PENDING)
          {
            add_ai(plci, &msg[0]);
            sig_req(plci,HANGUP,0);
            plci->State = OUTG_DIS_PENDING;
            return 1;
          }
        }
        else
        {
          if (plci->NL.Id && !plci->nl_remove_id)
          {
            mixer_remove (plci);
            nl_req_ncci(plci,REMOVE,0);
          sendf(appl,_DISCONNECT_R|CONFIRM,Id,Number,"w",0);
          sendf(appl, _DISCONNECT_I, Id, 0, "w", 0);
          plci->State = INC_DIS_PENDING;
          }
          return 1;
        }
      }
    }

  if(!appl)  return false;
  sendf(appl, _DISCONNECT_R|CONFIRM, Id, Number, "w",Info);
  return false;
}

byte disconnect_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  dbug(1,dprintf("disconnect_res"));
  if(plci)
  {
        /* clear ind mask bit, just in case of collsion of          */
        /* DISCONNECT_IND and CONNECT_RES                           */
    clear_c_ind_mask_bit (plci, (word)(appl->Id-1));
    ncci_free_receive_buffers (plci, 0);
    if(plci_remove_check(plci))
    {
      return 0;
    }
    if(plci->State==INC_DIS_PENDING
    || plci->State==SUSPENDING) {
      if(c_ind_mask_empty (plci)) {
        if(plci->State!=SUSPENDING)plci->State = IDLE;
        dbug(1,dprintf("chs=%d",plci->channels));
        if(!plci->channels) {
          plci_remove(plci);
        }
      }
    }
  }
  return 0;
}

byte listen_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word Info;
  byte i;

  dbug(1,dprintf("listen_req(Appl=0x%x)",appl->Id));

  Info = _WRONG_IDENTIFIER;
  if(a) {
    Info = 0;
    a->Info_Mask[appl->Id-1] = GET_DWORD(parms[0].info);
    a->CIP_Mask[appl->Id-1] = GET_DWORD(parms[1].info);
    dbug(1,dprintf("CIP_MASK=0x%lx",GET_DWORD(parms[1].info)));
    if (a->Info_Mask[appl->Id-1] & 0x200){ /* early B3 connect provides */
      a->Info_Mask[appl->Id-1] |=  0x10;   /* call progression infos    */
    }

    /* check if external controller listen and switch listen on or off*/
    if(Id&EXT_CONTROLLER && GET_DWORD(parms[1].info)){
      if(a->profile.Global_Options & ON_BOARD_CODEC) {
        dummy_plci.State = IDLE;
        a->codec_listen[appl->Id-1] = &dummy_plci;
        a->TelOAD[0] = (byte)(parms[3].length);
        for(i=1;parms[3].length>=i && i<22;i++) {
          a->TelOAD[i] = parms[3].info[i];
        }
        a->TelOAD[i] = 0;
        a->TelOSA[0] = (byte)(parms[4].length);
        for(i=1;parms[4].length>=i && i<22;i++) {
          a->TelOSA[i] = parms[4].info[i];
        }
        a->TelOSA[i] = 0;
      }
      else Info = 0x2002; /* wrong controller, codec not supported */
    }
    else{               /* clear listen */
      a->codec_listen[appl->Id-1] = (PLCI   *)0;
    }
  }
  sendf(appl,
        _LISTEN_R|CONFIRM,
        Id,
        Number,
        "w",Info);

  if (a) listen_check(a);
  return false;
}

byte info_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  word i;
  API_PARSE * ai;
  PLCI   * rc_plci = NULL;
    API_PARSE ai_parms[5];
  word Info = 0;

  dbug(1,dprintf("info_req"));
  for(i=0;i<5;i++) ai_parms[i].length = 0;

  ai = &msg[1];

  if(ai->length)
  {
    if(api_parse(&ai->info[1],(word)ai->length,"ssss",ai_parms))
    {
      dbug(1,dprintf("AddInfo wrong"));
      Info = _WRONG_MESSAGE_FORMAT;
    }
  }
  if(!a) Info = _WRONG_STATE;

  if(!Info && plci)
  {                /* no fac, with CPN, or KEY */
    rc_plci = plci;
    if(!ai_parms[3].length && plci->State && (msg[0].length || ai_parms[1].length) )
    {
      /* overlap sending option */
      dbug(1,dprintf("OvlSnd"));
      add_s(plci,CPN,&msg[0]);
      add_s(plci,KEY,&ai_parms[1]);
      sig_req(plci,INFO_REQ,0);
      send_req(plci);
      return false;
    }

    if(plci->State && ai_parms[2].length)
    {
      /* User_Info option */
      dbug(1,dprintf("UUI"));
      add_s(plci,UUI,&ai_parms[2]);
      sig_req(plci,USER_DATA,0);
    }
    else if(plci->State && ai_parms[3].length)
    {
      /* Facility option */
      dbug(1,dprintf("FAC"));
      add_s(plci,CPN,&msg[0]);
      add_ai(plci, &msg[1]);
      sig_req(plci,FACILITY_REQ,0);
    }
    else
    {
      Info = _WRONG_STATE;
    }
  }
  else if((ai_parms[1].length || ai_parms[2].length || ai_parms[3].length) && !Info)
  {
    /* NCR_Facility option -> send UUI and Keypad too */
    dbug(1,dprintf("NCR_FAC"));
    if((i=get_plci(a)))
    {
      rc_plci = &a->plci[i-1];
      appl->NullCREnable  = true;
      rc_plci->internal_command = C_NCR_FAC_REQ;
      rc_plci->appl = appl;
      add_p(rc_plci,CAI,"\x01\x80");
      add_p(rc_plci,UID,"\x06\x43\x61\x70\x69\x32\x30");
      sig_req(rc_plci,ASSIGN,DSIG_ID);
      send_req(rc_plci);
    }
    else
    {
      Info = _OUT_OF_PLCI;
    }

    if(!Info)
    {
      add_s(rc_plci,CPN,&msg[0]);
      add_ai(rc_plci, &msg[1]);
      sig_req(rc_plci,NCR_FACILITY,0);
      send_req(rc_plci);
      return false;
     /* for application controlled supplementary services    */
    }
  }

  if (!rc_plci)
  {
    Info = _WRONG_MESSAGE_FORMAT;
  }

  if(!Info)
  {
    send_req(rc_plci);
  }
  else
  {  /* appl is not assigned to a PLCI or error condition */
    dbug(1,dprintf("localInfoCon"));
    sendf(appl,
          _INFO_R|CONFIRM,
          Id,
          Number,
          "w",Info);
  }
  return false;
}

byte info_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  dbug(1,dprintf("info_res"));
  return false;
}

byte alert_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  word Info;
  byte ret;

  dbug(1,dprintf("alert_req"));

  Info = _WRONG_IDENTIFIER;
  ret = false;
  if(plci) {
    Info = _ALERT_IGNORED;
    if(plci->State!=INC_CON_ALERT) {
      Info = _WRONG_STATE;
      if(plci->State==INC_CON_PENDING) {
        Info = 0;
        plci->State=INC_CON_ALERT;
        add_ai(plci, &msg[0]);
        sig_req(plci,CALL_ALERT,0);
        ret = 1;
      }
    }
  }
  sendf(appl,
        _ALERT_R|CONFIRM,
        Id,
        Number,
        "w",Info);
  return ret;
}

byte facility_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  word Info = 0;
  word i    = 0;

  word selector;
  word SSreq;
  long relatedPLCIvalue;
  DIVA_CAPI_ADAPTER   * relatedadapter;
  byte * SSparms  = "";
    byte RCparms[]  = "\x05\x00\x00\x02\x00\x00";
    byte SSstruct[] = "\x09\x00\x00\x06\x00\x00\x00\x00\x00\x00";
  API_PARSE * parms;
    API_PARSE ss_parms[11];
  PLCI   *rplci;
    byte cai[15];
  dword d;
    API_PARSE dummy;

  dbug(1,dprintf("facility_req"));
  for(i=0;i<9;i++) ss_parms[i].length = 0;

  parms = &msg[1];

  if(!a)
  {
    dbug(1,dprintf("wrong Ctrl"));
    Info = _WRONG_IDENTIFIER;
  }

  selector = GET_WORD(msg[0].info);

  if(!Info)
  {
    switch(selector)
    {
      case SELECTOR_HANDSET:
        Info = AdvCodecSupport(a, plci, appl, HOOK_SUPPORT);
        break;

      case SELECTOR_SU_SERV:
        if(!msg[1].length)
        {
          Info = _WRONG_MESSAGE_FORMAT;
          break;
        }
        SSreq = GET_WORD(&(msg[1].info[1]));
        PUT_WORD(&RCparms[1],SSreq);
        SSparms = RCparms;
        switch(SSreq)
        {
          case S_GET_SUPPORTED_SERVICES:
            if((i=get_plci(a)))
            {
              rplci = &a->plci[i-1];
              rplci->appl = appl;
              add_p(rplci,CAI,"\x01\x80");
              add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
              sig_req(rplci,ASSIGN,DSIG_ID);
              send_req(rplci);
            }
            else
            {
              PUT_DWORD(&SSstruct[6], MASK_TERMINAL_PORTABILITY);
              SSparms = (byte *)SSstruct;
              break;
            }
            rplci->internal_command = GETSERV_REQ_PEND;
            rplci->number = Number;
            rplci->appl = appl;
            sig_req(rplci,S_SUPPORTED,0);
            send_req(rplci);
            return false;
            break;

          case S_LISTEN:
            if(parms->length==7)
            {
              if(api_parse(&parms->info[1],(word)parms->length,"wbd",ss_parms))
              {
                dbug(1,dprintf("format wrong"));
                Info = _WRONG_MESSAGE_FORMAT;
                break;
              }
            }
            else
            {
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            a->Notification_Mask[appl->Id-1] = GET_DWORD(ss_parms[2].info);
            if(a->Notification_Mask[appl->Id-1] & SMASK_MWI) /* MWI active? */
            {
              if((i=get_plci(a)))
              {
                rplci = &a->plci[i-1];
                rplci->appl = appl;
                add_p(rplci,CAI,"\x01\x80");
                add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
                sig_req(rplci,ASSIGN,DSIG_ID);
                send_req(rplci);
              }
              else
              {
                break;
              }
              rplci->internal_command = GET_MWI_STATE;
              rplci->number = Number;
              sig_req(rplci,MWI_POLL,0);
              send_req(rplci);
            }
            break;

          case S_HOLD:
            api_parse(&parms->info[1],(word)parms->length,"ws",ss_parms);
            if(plci && plci->State && plci->SuppState==IDLE)
            {
              plci->SuppState = HOLD_REQUEST;
              plci->command = C_HOLD_REQ;
              add_s(plci,CAI,&ss_parms[1]);
              sig_req(plci,CALL_HOLD,0);
              send_req(plci);
              return false;
            }
            else Info = 0x3010;                    /* wrong state           */
            break;
          case S_RETRIEVE:
            if(plci && plci->State && plci->SuppState==CALL_HELD)
            {
              if(Id & EXT_CONTROLLER)
              {
                if(AdvCodecSupport(a, plci, appl, 0))
                {
                  Info = 0x3010;                    /* wrong state           */
                  break;
                }
              }
              else plci->tel = 0;

              plci->SuppState = RETRIEVE_REQUEST;
              plci->command = C_RETRIEVE_REQ;
              if(plci->spoofed_msg==SPOOFING_REQUIRED)
              {
                plci->spoofed_msg = CALL_RETRIEVE;
                plci->internal_command = BLOCK_PLCI;
                plci->command = 0;
                dbug(1,dprintf("Spoof"));
                return false;
              }
              else
              {
                sig_req(plci,CALL_RETRIEVE,0);
                send_req(plci);
                return false;
              }
            }
            else Info = 0x3010;                    /* wrong state           */
            break;
          case S_SUSPEND:
            if(parms->length)
            {
              if(api_parse(&parms->info[1],(word)parms->length,"wbs",ss_parms))
              {
                dbug(1,dprintf("format wrong"));
                Info = _WRONG_MESSAGE_FORMAT;
                break;
              }
            }
            if(plci && plci->State)
            {
              add_s(plci,CAI,&ss_parms[2]);
              plci->command = SUSPEND_REQ;
              sig_req(plci,SUSPEND,0);
              plci->State = SUSPENDING;
              send_req(plci);
            }
            else Info = 0x3010;                    /* wrong state           */
            break;

          case S_RESUME:
            if(!(i=get_plci(a)) )
            {
              Info = _OUT_OF_PLCI;
              break;
            }
            rplci = &a->plci[i-1];
            rplci->appl = appl;
            rplci->number = Number;
            rplci->tel = 0;
            rplci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
            /* check 'external controller' bit for codec support */
            if(Id & EXT_CONTROLLER)
            {
              if(AdvCodecSupport(a, rplci, appl, 0) )
              {
                rplci->Id = 0;
                Info = 0x300A;
                break;
              }
            }
            if(parms->length)
            {
              if(api_parse(&parms->info[1],(word)parms->length,"wbs",ss_parms))
              {
                dbug(1,dprintf("format wrong"));
                rplci->Id = 0;
                Info = _WRONG_MESSAGE_FORMAT;
                break;
              }
            }
            dummy.length = 0;
            dummy.info = "\x00";
            add_b1(rplci, &dummy, 0, 0);
            if (a->Info_Mask[appl->Id-1] & 0x200)
            {
              /* early B3 connect (CIP mask bit 9) no release after a disc */
              add_p(rplci,LLI,"\x01\x01");
            }
            add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
            sig_req(rplci,ASSIGN,DSIG_ID);
            send_req(rplci);
            add_s(rplci,CAI,&ss_parms[2]);
            rplci->command = RESUME_REQ;
            sig_req(rplci,RESUME,0);
            rplci->State = RESUMING;
            send_req(rplci);
            break;

          case S_CONF_BEGIN: /* Request */
          case S_CONF_DROP:
          case S_CONF_ISOLATE:
          case S_CONF_REATTACH:
            if(api_parse(&parms->info[1],(word)parms->length,"wbd",ss_parms))
            {
              dbug(1,dprintf("format wrong"));
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            if(plci && plci->State && ((plci->SuppState==IDLE)||(plci->SuppState==CALL_HELD)))
            {
              d = GET_DWORD(ss_parms[2].info);     
              if(d>=0x80)
              {
                dbug(1,dprintf("format wrong"));
                Info = _WRONG_MESSAGE_FORMAT;
                break;
              }
              plci->ptyState = (byte)SSreq;
              plci->command = 0;
              cai[0] = 2;
              switch(SSreq)
              {
              case S_CONF_BEGIN:
                  cai[1] = CONF_BEGIN;
                  plci->internal_command = CONF_BEGIN_REQ_PEND;
                  break;
              case S_CONF_DROP:
                  cai[1] = CONF_DROP;
                  plci->internal_command = CONF_DROP_REQ_PEND;
                  break;
              case S_CONF_ISOLATE:
                  cai[1] = CONF_ISOLATE;
                  plci->internal_command = CONF_ISOLATE_REQ_PEND;
                  break;
              case S_CONF_REATTACH:
                  cai[1] = CONF_REATTACH;
                  plci->internal_command = CONF_REATTACH_REQ_PEND;
                  break;
              }
              cai[2] = (byte)d; /* Conference Size resp. PartyId */
              add_p(plci,CAI,cai);
              sig_req(plci,S_SERVICE,0);
              send_req(plci);
              return false;
            }
            else Info = 0x3010;                    /* wrong state           */
            break;

          case S_ECT:
          case S_3PTY_BEGIN:
          case S_3PTY_END:
          case S_CONF_ADD:
            if(parms->length==7)
            {
              if(api_parse(&parms->info[1],(word)parms->length,"wbd",ss_parms))
              {
                dbug(1,dprintf("format wrong"));
                Info = _WRONG_MESSAGE_FORMAT;
                break;
              }
            }
            else if(parms->length==8) /* workaround for the T-View-S */
            {
              if(api_parse(&parms->info[1],(word)parms->length,"wbdb",ss_parms))
              {
                dbug(1,dprintf("format wrong"));
                Info = _WRONG_MESSAGE_FORMAT;
                break;
              }
            }
            else
            {
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            if(!msg[1].length)
            {
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            if (!plci)
            {
              Info = _WRONG_IDENTIFIER;
              break;
            }
            relatedPLCIvalue = GET_DWORD(ss_parms[2].info);
            relatedPLCIvalue &= 0x0000FFFF;
            dbug(1,dprintf("PTY/ECT/addCONF,relPLCI=%lx",relatedPLCIvalue));
            /* controller starts with 0 up to (max_adapter - 1) */
            if (((relatedPLCIvalue & 0x7f) == 0)
             || (MapController ((byte)(relatedPLCIvalue & 0x7f)) == 0)
             || (MapController ((byte)(relatedPLCIvalue & 0x7f)) > max_adapter))
            {
              if(SSreq==S_3PTY_END)
              {
                dbug(1, dprintf("wrong Controller use 2nd PLCI=PLCI"));
                rplci = plci;
              }
              else
              {
                Info = 0x3010;                    /* wrong state           */
                break;
              }
            }
            else
            {  
              relatedadapter = &adapter[MapController ((byte)(relatedPLCIvalue & 0x7f))-1];
              relatedPLCIvalue >>=8;
              /* find PLCI PTR*/
              for(i=0,rplci=NULL;i<relatedadapter->max_plci;i++)
              {
                if(relatedadapter->plci[i].Id == (byte)relatedPLCIvalue)
                {
                  rplci = &relatedadapter->plci[i];
                }
              }
              if(!rplci || !relatedPLCIvalue)
              {
                if(SSreq==S_3PTY_END)
                {
                  dbug(1, dprintf("use 2nd PLCI=PLCI"));
                  rplci = plci;
                }
                else
                {
                  Info = 0x3010;                    /* wrong state           */
                  break;
                }
              }
            }
/*
            dbug(1,dprintf("rplci:%x",rplci));
            dbug(1,dprintf("plci:%x",plci));
            dbug(1,dprintf("rplci->ptyState:%x",rplci->ptyState));
            dbug(1,dprintf("plci->ptyState:%x",plci->ptyState));
            dbug(1,dprintf("SSreq:%x",SSreq));
            dbug(1,dprintf("rplci->internal_command:%x",rplci->internal_command));
            dbug(1,dprintf("rplci->appl:%x",rplci->appl));
            dbug(1,dprintf("rplci->Id:%x",rplci->Id));
*/
            /* send PTY/ECT req, cannot check all states because of US stuff */
            if( !rplci->internal_command && rplci->appl )
            {
              plci->command = 0;
              rplci->relatedPTYPLCI = plci;
              plci->relatedPTYPLCI = rplci;
              rplci->ptyState = (byte)SSreq;
              if(SSreq==S_ECT)
              {
                rplci->internal_command = ECT_REQ_PEND;
                cai[1] = ECT_EXECUTE;

                rplci->vswitchstate=0;
                rplci->vsprot=0;
                rplci->vsprotdialect=0;
                plci->vswitchstate=0;
                plci->vsprot=0;
                plci->vsprotdialect=0;

              }
              else if(SSreq==S_CONF_ADD)
              {
                rplci->internal_command = CONF_ADD_REQ_PEND;
                cai[1] = CONF_ADD;
              }
              else
              {
                rplci->internal_command = PTY_REQ_PEND;
                cai[1] = (byte)(SSreq-3);
              }
              rplci->number = Number;
              if(plci!=rplci) /* explicit invocation */
              {
                cai[0] = 2;
                cai[2] = plci->Sig.Id;
                dbug(1,dprintf("explicit invocation"));
              }
              else
              {
                dbug(1,dprintf("implicit invocation"));
                cai[0] = 1;
              }
              add_p(rplci,CAI,cai);
              sig_req(rplci,S_SERVICE,0);
              send_req(rplci);
              return false;
            }
            else
            {
              dbug(0,dprintf("Wrong line"));
              Info = 0x3010;                    /* wrong state           */
              break;
            }
            break;

          case S_CALL_DEFLECTION:
            if(api_parse(&parms->info[1],(word)parms->length,"wbwss",ss_parms))
            {
              dbug(1,dprintf("format wrong"));
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            if (!plci)
            {
              Info = _WRONG_IDENTIFIER;
              break;
            }
            /* reuse unused screening indicator */
            ss_parms[3].info[3] = (byte)GET_WORD(&(ss_parms[2].info[0]));
            plci->command = 0;
            plci->internal_command = CD_REQ_PEND;
            appl->CDEnable = true;
            cai[0] = 1;
            cai[1] = CALL_DEFLECTION;
            add_p(plci,CAI,cai);
            add_p(plci,CPN,ss_parms[3].info);
            sig_req(plci,S_SERVICE,0);
            send_req(plci);
            return false;
            break;

          case S_CALL_FORWARDING_START:
            if(api_parse(&parms->info[1],(word)parms->length,"wbdwwsss",ss_parms))
            {
              dbug(1,dprintf("format wrong"));
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }

            if((i=get_plci(a)))
            {
              rplci = &a->plci[i-1];
              rplci->appl = appl;
              add_p(rplci,CAI,"\x01\x80");
              add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
              sig_req(rplci,ASSIGN,DSIG_ID);
              send_req(rplci);
            }
            else
            {
              Info = _OUT_OF_PLCI;
              break;
            }

            /* reuse unused screening indicator */
            rplci->internal_command = CF_START_PEND;
            rplci->appl = appl;
            rplci->number = Number;
            appl->S_Handle = GET_DWORD(&(ss_parms[2].info[0]));
            cai[0] = 2;
            cai[1] = 0x70|(byte)GET_WORD(&(ss_parms[3].info[0])); /* Function */
            cai[2] = (byte)GET_WORD(&(ss_parms[4].info[0])); /* Basic Service */
            add_p(rplci,CAI,cai);
            add_p(rplci,OAD,ss_parms[5].info);
            add_p(rplci,CPN,ss_parms[6].info);
            sig_req(rplci,S_SERVICE,0);
            send_req(rplci);
            return false;
            break;

          case S_INTERROGATE_DIVERSION:
          case S_INTERROGATE_NUMBERS:
          case S_CALL_FORWARDING_STOP:
          case S_CCBS_REQUEST:
          case S_CCBS_DEACTIVATE:
          case S_CCBS_INTERROGATE:
            switch(SSreq)
            {
            case S_INTERROGATE_NUMBERS:
                if(api_parse(&parms->info[1],(word)parms->length,"wbd",ss_parms))
                {
                  dbug(0,dprintf("format wrong"));
                  Info = _WRONG_MESSAGE_FORMAT;
                }
                break;
            case S_CCBS_REQUEST:
            case S_CCBS_DEACTIVATE:
                if(api_parse(&parms->info[1],(word)parms->length,"wbdw",ss_parms))
                {
                  dbug(0,dprintf("format wrong"));
                  Info = _WRONG_MESSAGE_FORMAT;
                }
                break;
            case S_CCBS_INTERROGATE:
                if(api_parse(&parms->info[1],(word)parms->length,"wbdws",ss_parms))
                {
                  dbug(0,dprintf("format wrong"));
                  Info = _WRONG_MESSAGE_FORMAT;
                }
                break;
            default:
            if(api_parse(&parms->info[1],(word)parms->length,"wbdwws",ss_parms))
            {
              dbug(0,dprintf("format wrong"));
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
                break;
            }

            if(Info) break;
            if((i=get_plci(a)))
            {
              rplci = &a->plci[i-1];
              switch(SSreq)
              {
                case S_INTERROGATE_DIVERSION: /* use cai with S_SERVICE below */
                  cai[1] = 0x60|(byte)GET_WORD(&(ss_parms[3].info[0])); /* Function */
                  rplci->internal_command = INTERR_DIVERSION_REQ_PEND; /* move to rplci if assigned */
                  break;
                case S_INTERROGATE_NUMBERS: /* use cai with S_SERVICE below */
                  cai[1] = DIVERSION_INTERROGATE_NUM; /* Function */
                  rplci->internal_command = INTERR_NUMBERS_REQ_PEND; /* move to rplci if assigned */
                  break;
                case S_CALL_FORWARDING_STOP:
                  rplci->internal_command = CF_STOP_PEND;
                  cai[1] = 0x80|(byte)GET_WORD(&(ss_parms[3].info[0])); /* Function */
                  break;
                case S_CCBS_REQUEST:
                  cai[1] = CCBS_REQUEST;
                  rplci->internal_command = CCBS_REQUEST_REQ_PEND;
                  break;
                case S_CCBS_DEACTIVATE:
                  cai[1] = CCBS_DEACTIVATE;
                  rplci->internal_command = CCBS_DEACTIVATE_REQ_PEND;
                  break;
                case S_CCBS_INTERROGATE:
                  cai[1] = CCBS_INTERROGATE;
                  rplci->internal_command = CCBS_INTERROGATE_REQ_PEND;
                  break;
                default:
                  cai[1] = 0;
                break;
              }
              rplci->appl = appl;
              rplci->number = Number;
              add_p(rplci,CAI,"\x01\x80");
              add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
              sig_req(rplci,ASSIGN,DSIG_ID);
              send_req(rplci);
            }
            else
            {
              Info = _OUT_OF_PLCI;
              break;
            }

            appl->S_Handle = GET_DWORD(&(ss_parms[2].info[0]));
            switch(SSreq)
            {
            case S_INTERROGATE_NUMBERS:
                cai[0] = 1;
                add_p(rplci,CAI,cai);
                break;
            case S_CCBS_REQUEST:
            case S_CCBS_DEACTIVATE:
                cai[0] = 3;
                PUT_WORD(&cai[2],GET_WORD(&(ss_parms[3].info[0])));
                add_p(rplci,CAI,cai);
                break;
            case S_CCBS_INTERROGATE:
                cai[0] = 3;
                PUT_WORD(&cai[2],GET_WORD(&(ss_parms[3].info[0])));
                add_p(rplci,CAI,cai);
                add_p(rplci,OAD,ss_parms[4].info);
                break;
            default:
            cai[0] = 2;
            cai[2] = (byte)GET_WORD(&(ss_parms[4].info[0])); /* Basic Service */
            add_p(rplci,CAI,cai);
            add_p(rplci,OAD,ss_parms[5].info);
                break;
            }
                        
            sig_req(rplci,S_SERVICE,0);
            send_req(rplci);
            return false;
            break;

          case S_MWI_ACTIVATE:
            if(api_parse(&parms->info[1],(word)parms->length,"wbwdwwwssss",ss_parms))
            {
              dbug(1,dprintf("format wrong"));
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            if(!plci)
            {                               
              if((i=get_plci(a)))
              {
                rplci = &a->plci[i-1];
                rplci->appl = appl;
                rplci->cr_enquiry=true;
                add_p(rplci,CAI,"\x01\x80");
                add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
                sig_req(rplci,ASSIGN,DSIG_ID);
                send_req(rplci);
              }
              else
              {
                Info = _OUT_OF_PLCI;
                break;
              }
            }
            else
            {
              rplci = plci;
              rplci->cr_enquiry=false;
            }

            rplci->command = 0;
            rplci->internal_command = MWI_ACTIVATE_REQ_PEND;
            rplci->appl = appl;
            rplci->number = Number;

            cai[0] = 13;
            cai[1] = ACTIVATION_MWI; /* Function */
            PUT_WORD(&cai[2],GET_WORD(&(ss_parms[2].info[0]))); /* Basic Service */
            PUT_DWORD(&cai[4],GET_DWORD(&(ss_parms[3].info[0]))); /* Number of Messages */
            PUT_WORD(&cai[8],GET_WORD(&(ss_parms[4].info[0]))); /* Message Status */
            PUT_WORD(&cai[10],GET_WORD(&(ss_parms[5].info[0]))); /* Message Reference */
            PUT_WORD(&cai[12],GET_WORD(&(ss_parms[6].info[0]))); /* Invocation Mode */
            add_p(rplci,CAI,cai);
            add_p(rplci,CPN,ss_parms[7].info); /* Receiving User Number */
            add_p(rplci,OAD,ss_parms[8].info); /* Controlling User Number */
            add_p(rplci,OSA,ss_parms[9].info); /* Controlling User Provided Number */
            add_p(rplci,UID,ss_parms[10].info); /* Time */
            sig_req(rplci,S_SERVICE,0);
            send_req(rplci);
            return false;

          case S_MWI_DEACTIVATE:
            if(api_parse(&parms->info[1],(word)parms->length,"wbwwss",ss_parms))
            {
              dbug(1,dprintf("format wrong"));
              Info = _WRONG_MESSAGE_FORMAT;
              break;
            }
            if(!plci)
            {                               
              if((i=get_plci(a)))
              {
                rplci = &a->plci[i-1];
                rplci->appl = appl;
                rplci->cr_enquiry=true;
                add_p(rplci,CAI,"\x01\x80");
                add_p(rplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
                sig_req(rplci,ASSIGN,DSIG_ID);
                send_req(rplci);
              }
              else
              {
                Info = _OUT_OF_PLCI;
                break;
              }
            }
            else
            {
              rplci = plci;
              rplci->cr_enquiry=false;
            }

            rplci->command = 0;
            rplci->internal_command = MWI_DEACTIVATE_REQ_PEND;
            rplci->appl = appl;
            rplci->number = Number;

            cai[0] = 5;
            cai[1] = DEACTIVATION_MWI; /* Function */
            PUT_WORD(&cai[2],GET_WORD(&(ss_parms[2].info[0]))); /* Basic Service */
            PUT_WORD(&cai[4],GET_WORD(&(ss_parms[3].info[0]))); /* Invocation Mode */
            add_p(rplci,CAI,cai);
            add_p(rplci,CPN,ss_parms[4].info); /* Receiving User Number */
            add_p(rplci,OAD,ss_parms[5].info); /* Controlling User Number */
            sig_req(rplci,S_SERVICE,0);
            send_req(rplci);
            return false;

          default:
            Info = 0x300E;  /* not supported */
            break;
        }
        break; /* case SELECTOR_SU_SERV: end */


      case SELECTOR_DTMF:
        return (dtmf_request (Id, Number, a, plci, appl, msg));



      case SELECTOR_LINE_INTERCONNECT:
        return (mixer_request (Id, Number, a, plci, appl, msg));



      case PRIV_SELECTOR_ECHO_CANCELLER:
        appl->appl_flags |= APPL_FLAG_PRIV_EC_SPEC;
        return (ec_request (Id, Number, a, plci, appl, msg));

      case SELECTOR_ECHO_CANCELLER:
        appl->appl_flags &= ~APPL_FLAG_PRIV_EC_SPEC;
        return (ec_request (Id, Number, a, plci, appl, msg));


      case SELECTOR_V42BIS:
      default:
        Info = _FACILITY_NOT_SUPPORTED;
        break;
    } /* end of switch(selector) */
  }

  dbug(1,dprintf("SendFacRc"));
  sendf(appl,
        _FACILITY_R|CONFIRM,
        Id,
        Number,
        "wws",Info,selector,SSparms);
  return false;
}

byte facility_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  dbug(1,dprintf("facility_res"));
  return false;
}

byte connect_b3_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word Info = 0;
  byte req;
  byte len;
  word w;
  word fax_control_bits, fax_feature_bits, fax_info_change;
  API_PARSE * ncpi;
    byte pvc[2];

    API_PARSE fax_parms[9];
  word i;


  dbug(1,dprintf("connect_b3_req"));
  if(plci)
  {
    if ((plci->State == IDLE) || (plci->State == OUTG_DIS_PENDING)
     || (plci->State == INC_DIS_PENDING) || (plci->SuppState != IDLE))
    {
      Info = _WRONG_STATE;
    }
    else
    {
      /* local reply if assign unsuccessfull
         or B3 protocol allows only one layer 3 connection
           and already connected
             or B2 protocol not any LAPD
               and connect_b3_req contradicts originate/answer direction */
      if (!plci->NL.Id
       || (((plci->B3_prot != B3_T90NL) && (plci->B3_prot != B3_ISO8208) && (plci->B3_prot != B3_X25_DCE))
        && ((plci->channels != 0)
         || (((plci->B2_prot != B2_SDLC) && (plci->B2_prot != B2_LAPD) && (plci->B2_prot != B2_LAPD_FREE_SAPI_SEL))
          && ((plci->call_dir & CALL_DIR_ANSWER) && !(plci->call_dir & CALL_DIR_FORCE_OUTG_NL))))))
      {
        dbug(1,dprintf("B3 already connected=%d or no NL.Id=0x%x, dir=%d sstate=0x%x",
                       plci->channels,plci->NL.Id,plci->call_dir,plci->SuppState));
        Info = _WRONG_STATE;
        sendf(appl,                                                        
              _CONNECT_B3_R|CONFIRM,
              Id,
              Number,
              "w",Info);
        return false;
      }
      plci->requested_options_conn = 0;

      req = N_CONNECT;
      ncpi = &parms[0];
      if(plci->B3_prot==2 || plci->B3_prot==3)
      {
        if(ncpi->length>2)
        {
          /* check for PVC */
          if(ncpi->info[2] || ncpi->info[3])
          {
            pvc[0] = ncpi->info[3];
            pvc[1] = ncpi->info[2];
            add_d(plci,2,pvc);
            req = N_RESET;
          }
          else
          {
            if(ncpi->info[1] &1) req = N_CONNECT | N_D_BIT;
            add_d(plci,(word)(ncpi->length-3),&ncpi->info[4]);
          }
        }
      }
      else if(plci->B3_prot==5)
      {
        if (plci->NL.Id && !plci->nl_remove_id)
        {
          fax_control_bits = GET_WORD(&((T30_INFO   *)plci->fax_connect_info_buffer)->control_bits_low);
          fax_feature_bits = GET_WORD(&((T30_INFO   *)plci->fax_connect_info_buffer)->feature_bits_low);
          if (!(fax_control_bits & T30_CONTROL_BIT_MORE_DOCUMENTS)
           || (fax_feature_bits & T30_FEATURE_BIT_MORE_DOCUMENTS))
          {
            len = (byte)(&(((T30_INFO *) 0)->universal_6));
            fax_info_change = false;
            if (ncpi->length >= 4)
            {
              w = GET_WORD(&ncpi->info[3]);
              if ((w & 0x0001) != ((word)(((T30_INFO   *)(plci->fax_connect_info_buffer))->resolution & 0x0001)))
              {
                ((T30_INFO   *)(plci->fax_connect_info_buffer))->resolution =
                  (byte)((((T30_INFO   *)(plci->fax_connect_info_buffer))->resolution & ~T30_RESOLUTION_R8_0770_OR_200) |
                  ((w & 0x0001) ? T30_RESOLUTION_R8_0770_OR_200 : 0));
                fax_info_change = true;
              }
              fax_control_bits &= ~(T30_CONTROL_BIT_REQUEST_POLLING | T30_CONTROL_BIT_MORE_DOCUMENTS);
              if (w & 0x0002)  /* Fax-polling request */
                fax_control_bits |= T30_CONTROL_BIT_REQUEST_POLLING;
              if ((w & 0x0004) /* Request to send / poll another document */
               && (a->manufacturer_features & MANUFACTURER_FEATURE_FAX_MORE_DOCUMENTS))
              {
                fax_control_bits |= T30_CONTROL_BIT_MORE_DOCUMENTS;
              }
              if (ncpi->length >= 6)
              {
                w = GET_WORD(&ncpi->info[5]);
                if (((byte) w) != ((T30_INFO   *)(plci->fax_connect_info_buffer))->data_format)
                {
                  ((T30_INFO   *)(plci->fax_connect_info_buffer))->data_format = (byte) w;
                  fax_info_change = true;
                }

                if ((a->man_profile.private_options & (1L << PRIVATE_FAX_SUB_SEP_PWD))
                 && (GET_WORD(&ncpi->info[5]) & 0x8000)) /* Private SEP/SUB/PWD enable */
                {
                  plci->requested_options_conn |= (1L << PRIVATE_FAX_SUB_SEP_PWD);
                }
                if ((a->man_profile.private_options & (1L << PRIVATE_FAX_NONSTANDARD))
                 && (GET_WORD(&ncpi->info[5]) & 0x4000)) /* Private non-standard facilities enable */
                {
                  plci->requested_options_conn |= (1L << PRIVATE_FAX_NONSTANDARD);
                }
                fax_control_bits &= ~(T30_CONTROL_BIT_ACCEPT_SUBADDRESS | T30_CONTROL_BIT_ACCEPT_SEL_POLLING |
                  T30_CONTROL_BIT_ACCEPT_PASSWORD);
                if ((plci->requested_options_conn | plci->requested_options | a->requested_options_table[appl->Id-1])
                  & ((1L << PRIVATE_FAX_SUB_SEP_PWD) | (1L << PRIVATE_FAX_NONSTANDARD)))
                {
                  if (api_parse (&ncpi->info[1], ncpi->length, "wwwwsss", fax_parms))
                    Info = _WRONG_MESSAGE_FORMAT;
                  else
                  {
                    if ((plci->requested_options_conn | plci->requested_options | a->requested_options_table[appl->Id-1])
                      & (1L << PRIVATE_FAX_SUB_SEP_PWD))
      {
                    fax_control_bits |= T30_CONTROL_BIT_ACCEPT_SUBADDRESS | T30_CONTROL_BIT_ACCEPT_PASSWORD;
                    if (fax_control_bits & T30_CONTROL_BIT_ACCEPT_POLLING)
                      fax_control_bits |= T30_CONTROL_BIT_ACCEPT_SEL_POLLING;
      }
                    w = fax_parms[4].length;
                    if (w > 20)
                      w = 20;
                    ((T30_INFO   *)(plci->fax_connect_info_buffer))->station_id_len = (byte) w;
                    for (i = 0; i < w; i++)
                      ((T30_INFO   *)(plci->fax_connect_info_buffer))->station_id[i] = fax_parms[4].info[1+i];
                    ((T30_INFO   *)(plci->fax_connect_info_buffer))->head_line_len = 0;
                    len = (byte)(((T30_INFO *) 0)->station_id + 20);
                    w = fax_parms[5].length;
                    if (w > 20)
                      w = 20;
                    plci->fax_connect_info_buffer[len++] = (byte) w;
                    for (i = 0; i < w; i++)
                      plci->fax_connect_info_buffer[len++] = fax_parms[5].info[1+i];
                    w = fax_parms[6].length;
                    if (w > 20)
                      w = 20;
                    plci->fax_connect_info_buffer[len++] = (byte) w;
                    for (i = 0; i < w; i++)
                      plci->fax_connect_info_buffer[len++] = fax_parms[6].info[1+i];
                    if ((plci->requested_options_conn | plci->requested_options | a->requested_options_table[appl->Id-1])
                      & (1L << PRIVATE_FAX_NONSTANDARD))
      {
                      if (api_parse (&ncpi->info[1], ncpi->length, "wwwwssss", fax_parms))
        {
                        dbug(1,dprintf("non-standard facilities info missing or wrong format"));
                        plci->fax_connect_info_buffer[len++] = 0;
        }
                      else
                      {
          if ((fax_parms[7].length >= 3) && (fax_parms[7].info[1] >= 2))
            plci->nsf_control_bits = GET_WORD(&fax_parms[7].info[2]);
   plci->fax_connect_info_buffer[len++] = (byte)(fax_parms[7].length);
          for (i = 0; i < fax_parms[7].length; i++)
     plci->fax_connect_info_buffer[len++] = fax_parms[7].info[1+i];
                      }
                    }
                  }
                }
                else
                {
                  len = (byte)(&(((T30_INFO *) 0)->universal_6));
                }
                fax_info_change = true;

              }
              if (fax_control_bits != GET_WORD(&((T30_INFO   *)plci->fax_connect_info_buffer)->control_bits_low))
              {
                PUT_WORD (&((T30_INFO   *)plci->fax_connect_info_buffer)->control_bits_low, fax_control_bits);
                fax_info_change = true;
              }
            }
            if (Info == GOOD)
            {
              plci->fax_connect_info_length = len;
              if (fax_info_change)
              {
                if (fax_feature_bits & T30_FEATURE_BIT_MORE_DOCUMENTS)
                {
                  start_internal_command (Id, plci, fax_connect_info_command);
                  return false;
                }
                else
                {
                  start_internal_command (Id, plci, fax_adjust_b23_command);
                  return false;
                }
              }
            }
          }
          else  Info = _WRONG_STATE;
        }
        else  Info = _WRONG_STATE;
      }

      else if (plci->B3_prot == B3_RTP)
      {
        plci->internal_req_buffer[0] = ncpi->length + 1;
        plci->internal_req_buffer[1] = UDATA_REQUEST_RTP_RECONFIGURE;
        for (w = 0; w < ncpi->length; w++)
          plci->internal_req_buffer[2+w] = ncpi->info[1+w];
        start_internal_command (Id, plci, rtp_connect_b3_req_command);
        return false;
      }

      if(!Info)
      {
        nl_req_ncci(plci,req,0);
        return 1;
      }
    }
  }
  else Info = _WRONG_IDENTIFIER;

  sendf(appl,
        _CONNECT_B3_R|CONFIRM,
        Id,
        Number,
        "w",Info);
  return false;
}

byte connect_b3_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word ncci;
  API_PARSE * ncpi;
  byte req;

  word w;


    API_PARSE fax_parms[9];
  word i;
  byte len;


  dbug(1,dprintf("connect_b3_res"));

  ncci = (word)(Id>>16);
  if(plci && ncci) {
    if(a->ncci_state[ncci]==INC_CON_PENDING) {
      if (GET_WORD (&parms[0].info[0]) != 0)
      {
        a->ncci_state[ncci] = OUTG_REJ_PENDING;
        channel_request_xon (plci, a->ncci_ch[ncci]);
        channel_xmit_xon (plci);
        cleanup_ncci_data (plci, ncci);
        nl_req_ncci(plci,N_DISC,(byte)ncci);
        return 1;
      }
      a->ncci_state[ncci] = INC_ACT_PENDING;

      req = N_CONNECT_ACK;
      ncpi = &parms[1];
      if ((plci->B3_prot == 4) || (plci->B3_prot == 5) || (plci->B3_prot == 7))
      {

        if ((plci->requested_options_conn | plci->requested_options | a->requested_options_table[plci->appl->Id-1])
          & (1L << PRIVATE_FAX_NONSTANDARD))
 {
   if (((plci->B3_prot == 4) || (plci->B3_prot == 5))
    && (plci->nsf_control_bits & T30_NSF_CONTROL_BIT_ENABLE_NSF)
    && (plci->nsf_control_bits & T30_NSF_CONTROL_BIT_NEGOTIATE_RESP))
   {
            len = ((byte)(((T30_INFO *) 0)->station_id + 20));
            if (plci->fax_connect_info_length < len)
            {
              ((T30_INFO *)(plci->fax_connect_info_buffer))->station_id_len = 0;
              ((T30_INFO *)(plci->fax_connect_info_buffer))->head_line_len = 0;
            }
            if (api_parse (&ncpi->info[1], ncpi->length, "wwwwssss", fax_parms))
            {
              dbug(1,dprintf("non-standard facilities info missing or wrong format"));
            }
            else
            {
              if (plci->fax_connect_info_length <= len)
                plci->fax_connect_info_buffer[len] = 0;
              len += 1 + plci->fax_connect_info_buffer[len];
              if (plci->fax_connect_info_length <= len)
                plci->fax_connect_info_buffer[len] = 0;
              len += 1 + plci->fax_connect_info_buffer[len];
              if ((fax_parms[7].length >= 3) && (fax_parms[7].info[1] >= 2))
                plci->nsf_control_bits = GET_WORD(&fax_parms[7].info[2]);
              plci->fax_connect_info_buffer[len++] = (byte)(fax_parms[7].length);
              for (i = 0; i < fax_parms[7].length; i++)
                plci->fax_connect_info_buffer[len++] = fax_parms[7].info[1+i];
            }
            plci->fax_connect_info_length = len;
            ((T30_INFO *)(plci->fax_connect_info_buffer))->code = 0;
            start_internal_command (Id, plci, fax_connect_ack_command);
     return false;
          }
        }

        nl_req_ncci(plci,req,(byte)ncci);
        if ((plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
         && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
        {
          if (plci->B3_prot == 4)
            sendf(appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
          else
            sendf(appl,_CONNECT_B3_ACTIVE_I,Id,0,"S",plci->ncpi_buffer);
          plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
        }
      }

      else if (plci->B3_prot == B3_RTP)
      {
        plci->internal_req_buffer[0] = ncpi->length + 1;
        plci->internal_req_buffer[1] = UDATA_REQUEST_RTP_RECONFIGURE;
        for (w = 0; w < ncpi->length; w++)
          plci->internal_req_buffer[2+w] = ncpi->info[1+w];
        start_internal_command (Id, plci, rtp_connect_b3_res_command);
        return false;
      }

      else
      {
        if(ncpi->length>2) {
          if(ncpi->info[1] &1) req = N_CONNECT_ACK | N_D_BIT;
          add_d(plci,(word)(ncpi->length-3),&ncpi->info[4]);
        }
        nl_req_ncci(plci,req,(byte)ncci);
        sendf(appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
        if (plci->adjust_b_restore)
        {
          plci->adjust_b_restore = false;
          start_internal_command (Id, plci, adjust_b_restore);
        }
      }
      return 1;
    }
  }
  return false;
}

byte connect_b3_a_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word ncci;

  ncci = (word)(Id>>16);
  dbug(1,dprintf("connect_b3_a_res(ncci=0x%x)",ncci));

  if (plci && ncci && (plci->State != IDLE) && (plci->State != INC_DIS_PENDING)
   && (plci->State != OUTG_DIS_PENDING))
  {
    if(a->ncci_state[ncci]==INC_ACT_PENDING) {
      a->ncci_state[ncci] = CONNECTED;
      if(plci->State!=INC_CON_CONNECTED_ALERT) plci->State = CONNECTED;
      channel_request_xon (plci, a->ncci_ch[ncci]);
      channel_xmit_xon (plci);
    }
  }
  return false;
}

byte disconnect_b3_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word Info;
  word ncci;
  API_PARSE * ncpi;

  dbug(1,dprintf("disconnect_b3_req"));

  Info = _WRONG_IDENTIFIER;
  ncci = (word)(Id>>16);
  if (plci && ncci)
  {
    Info = _WRONG_STATE;
    if ((a->ncci_state[ncci] == CONNECTED)
     || (a->ncci_state[ncci] == OUTG_CON_PENDING)
     || (a->ncci_state[ncci] == INC_CON_PENDING)
     || (a->ncci_state[ncci] == INC_ACT_PENDING))
    {
      a->ncci_state[ncci] = OUTG_DIS_PENDING;
      channel_request_xon (plci, a->ncci_ch[ncci]);
      channel_xmit_xon (plci);

      if (a->ncci[ncci].data_pending
       && ((plci->B3_prot == B3_TRANSPARENT)
        || (plci->B3_prot == B3_T30)
        || (plci->B3_prot == B3_T30_WITH_EXTENSIONS)))
      {
        plci->send_disc = (byte)ncci;
        plci->command = 0;
        return false;
      }
      else
      {
        cleanup_ncci_data (plci, ncci);

        if(plci->B3_prot==2 || plci->B3_prot==3)
        {
          ncpi = &parms[0];
          if(ncpi->length>3)
          {
            add_d(plci, (word)(ncpi->length - 3) ,(byte   *)&(ncpi->info[4]));
          }
        }
        nl_req_ncci(plci,N_DISC,(byte)ncci);
      }
      return 1;
    }
  }
  sendf(appl,
        _DISCONNECT_B3_R|CONFIRM,
        Id,
        Number,
        "w",Info);
  return false;
}

byte disconnect_b3_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word ncci;
  word i;

  ncci = (word)(Id>>16);
  dbug(1,dprintf("disconnect_b3_res(ncci=0x%x",ncci));
  if(plci && ncci) {
    plci->requested_options_conn = 0;
    plci->fax_connect_info_length = 0;
    plci->ncpi_state = 0x00;
    if (((plci->B3_prot != B3_T90NL) && (plci->B3_prot != B3_ISO8208) && (plci->B3_prot != B3_X25_DCE))
      && ((plci->B2_prot != B2_LAPD) && (plci->B2_prot != B2_LAPD_FREE_SAPI_SEL)))
    {
      plci->call_dir |= CALL_DIR_FORCE_OUTG_NL;
    }
    for(i=0; i<MAX_CHANNELS_PER_PLCI && plci->inc_dis_ncci_table[i]!=(byte)ncci; i++);
    if(i<MAX_CHANNELS_PER_PLCI) {
      if(plci->channels)plci->channels--;
      for(; i<MAX_CHANNELS_PER_PLCI-1; i++) plci->inc_dis_ncci_table[i] = plci->inc_dis_ncci_table[i+1];
      plci->inc_dis_ncci_table[MAX_CHANNELS_PER_PLCI-1] = 0;

      ncci_free_receive_buffers (plci, ncci);

      if((plci->State==IDLE || plci->State==SUSPENDING) && !plci->channels){
        if(plci->State == SUSPENDING){
          sendf(plci->appl,
                _FACILITY_I,
                Id & 0xffffL,
                0,
                "ws", (word)3, "\x03\x04\x00\x00");
          sendf(plci->appl, _DISCONNECT_I, Id & 0xffffL, 0, "w", 0);
        }
        plci_remove(plci);
        plci->State=IDLE;
      }
    }
    else
    {
      if ((a->manufacturer_features & MANUFACTURER_FEATURE_FAX_PAPER_FORMATS)
       && ((plci->B3_prot == 4) || (plci->B3_prot == 5))
       && (a->ncci_state[ncci] == INC_DIS_PENDING))
      {
        ncci_free_receive_buffers (plci, ncci);

        nl_req_ncci(plci,N_EDATA,(byte)ncci);

        plci->adapter->ncci_state[ncci] = IDLE;
        start_internal_command (Id, plci, fax_disconnect_command);
        return 1;
      }
    }
  }
  return false;
}

byte data_b3_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  NCCI   *ncci_ptr;
  DATA_B3_DESC   *data;
  word Info;
  word ncci;
  word i;

  dbug(1,dprintf("data_b3_req"));

  Info = _WRONG_IDENTIFIER;
  ncci = (word)(Id>>16);
  dbug(1,dprintf("ncci=0x%x, plci=0x%x",ncci,plci));

  if (plci && ncci)
  {
    Info = _WRONG_STATE;
    if ((a->ncci_state[ncci] == CONNECTED)
     || (a->ncci_state[ncci] == INC_ACT_PENDING))
    {
        /* queue data */
      ncci_ptr = &(a->ncci[ncci]);
      i = ncci_ptr->data_out + ncci_ptr->data_pending;
      if (i >= MAX_DATA_B3)
        i -= MAX_DATA_B3;
      data = &(ncci_ptr->DBuffer[i]);
      data->Number = Number;
      if ((((byte   *)(parms[0].info)) >= ((byte   *)(plci->msg_in_queue)))
       && (((byte   *)(parms[0].info)) < ((byte   *)(plci->msg_in_queue)) + sizeof(plci->msg_in_queue)))
      {

        data->P = (byte *)(long)(*((dword *)(parms[0].info)));

      }
      else
        data->P = TransmitBufferSet(appl,*(dword *)parms[0].info);
      data->Length = GET_WORD(parms[1].info);
      data->Handle = GET_WORD(parms[2].info);
      data->Flags = GET_WORD(parms[3].info);
      (ncci_ptr->data_pending)++;

        /* check for delivery confirmation */
      if (data->Flags & 0x0004)
      {
        i = ncci_ptr->data_ack_out + ncci_ptr->data_ack_pending;
        if (i >= MAX_DATA_ACK)
          i -= MAX_DATA_ACK;
        ncci_ptr->DataAck[i].Number = data->Number;
        ncci_ptr->DataAck[i].Handle = data->Handle;
        (ncci_ptr->data_ack_pending)++;
      }

      send_data(plci);
      return false;
    }
  }
  if (appl)
  {
    if (plci)
    {
      if ((((byte   *)(parms[0].info)) >= ((byte   *)(plci->msg_in_queue)))
       && (((byte   *)(parms[0].info)) < ((byte   *)(plci->msg_in_queue)) + sizeof(plci->msg_in_queue)))
      {

        TransmitBufferFree (appl, (byte *)(long)(*((dword *)(parms[0].info))));

      }
    }
    sendf(appl,
          _DATA_B3_R|CONFIRM,
          Id,
          Number,
          "ww",GET_WORD(parms[2].info),Info);
  }
  return false;
}

byte data_b3_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word n;
  word ncci;
  word NCCIcode;

  dbug(1,dprintf("data_b3_res"));

  ncci = (word)(Id>>16);
  if(plci && ncci) {
    n = GET_WORD(parms[0].info);
    dbug(1,dprintf("free(%d)",n));
    NCCIcode = ncci | (((word) a->Id) << 8);
    if(n<appl->MaxBuffer &&
       appl->DataNCCI[n]==NCCIcode &&
       (byte)(appl->DataFlags[n]>>8)==plci->Id) {
      dbug(1,dprintf("found"));
      appl->DataNCCI[n] = 0;

      if (channel_can_xon (plci, a->ncci_ch[ncci])) {
        channel_request_xon (plci, a->ncci_ch[ncci]);
      }
      channel_xmit_xon (plci);

      if(appl->DataFlags[n] &4) {
        nl_req_ncci(plci,N_DATA_ACK,(byte)ncci);
        return 1;
      }
    }
  }
  return false;
}

byte reset_b3_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word Info;
  word ncci;

  dbug(1,dprintf("reset_b3_req"));

  Info = _WRONG_IDENTIFIER;
  ncci = (word)(Id>>16);
  if(plci && ncci)
  {
    Info = _WRONG_STATE;
    switch (plci->B3_prot)
    {
    case B3_ISO8208:
    case B3_X25_DCE:
      if(a->ncci_state[ncci]==CONNECTED)
      {
        nl_req_ncci(plci,N_RESET,(byte)ncci);
        send_req(plci);
        Info = GOOD;
      }
      break;
    case B3_TRANSPARENT:
      if(a->ncci_state[ncci]==CONNECTED)
      {
        start_internal_command (Id, plci, reset_b3_command);
        Info = GOOD;
      }
      break;
    }
  }
  /* reset_b3 must result in a reset_b3_con & reset_b3_Ind */
  sendf(appl,
        _RESET_B3_R|CONFIRM,
        Id,
        Number,
        "w",Info);
  return false;
}

byte reset_b3_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word ncci;

  dbug(1,dprintf("reset_b3_res"));

  ncci = (word)(Id>>16);
  if(plci && ncci) {
    switch (plci->B3_prot)
    {
    case B3_ISO8208:
    case B3_X25_DCE:
      if(a->ncci_state[ncci]==INC_RES_PENDING)
      {
        a->ncci_state[ncci] = CONNECTED;
        nl_req_ncci(plci,N_RESET_ACK,(byte)ncci);
        return true;
      }
    break;
    }
  }
  return false;
}

byte connect_b3_t90_a_res(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * parms)
{
  word ncci;
  API_PARSE * ncpi;
  byte req;

  dbug(1,dprintf("connect_b3_t90_a_res"));

  ncci = (word)(Id>>16);
  if(plci && ncci) {
    if(a->ncci_state[ncci]==INC_ACT_PENDING) {
      a->ncci_state[ncci] = CONNECTED;
    }
    else if(a->ncci_state[ncci]==INC_CON_PENDING) {
      a->ncci_state[ncci] = CONNECTED;

      req = N_CONNECT_ACK;

        /* parms[0]==0 for CAPI original message definition! */
      if(parms[0].info) {
        ncpi = &parms[1];
        if(ncpi->length>2) {
          if(ncpi->info[1] &1) req = N_CONNECT_ACK | N_D_BIT;
          add_d(plci,(word)(ncpi->length-3),&ncpi->info[4]);
        }
      }
      nl_req_ncci(plci,req,(byte)ncci);
      return 1;
    }
  }
  return false;
}


byte select_b_req(dword Id, word Number, DIVA_CAPI_ADAPTER   * a, PLCI   * plci, APPL   * appl, API_PARSE * msg)
{
  word Info=0;
  word i;
  byte tel;
    API_PARSE bp_parms[7];

  if(!plci || !msg)
  {
    Info = _WRONG_IDENTIFIER;
  }
  else
  {
    dbug(1,dprintf("select_b_req[%d],PLCI=0x%x,Tel=0x%x,NL=0x%x,appl=0x%x,sstate=0x%x",
                   msg->length,plci->Id,plci->tel,plci->NL.Id,plci->appl,plci->SuppState));
    dbug(1,dprintf("PlciState=0x%x",plci->State));
    for(i=0;i<7;i++) bp_parms[i].length = 0;

    /* check if no channel is open, no B3 connected only */
    if((plci->State == IDLE) || (plci->State == OUTG_DIS_PENDING) || (plci->State == INC_DIS_PENDING)
     || (plci->SuppState != IDLE) || plci->channels || plci->nl_remove_id)
    {
      Info = _WRONG_STATE;
    }
    /* check message format and fill bp_parms pointer */
    else if(msg->length && api_parse(&msg->info[1], (word)msg->length, "wwwsss", bp_parms))
    {
      Info = _WRONG_MESSAGE_FORMAT;
    }
    else
    {
      if((plci->State==INC_CON_PENDING) || (plci->State==INC_CON_ALERT)) /* send alert tone inband to the network, */
      {                                                                  /* e.g. Qsig or RBS or Cornet-N or xess PRI */
        if(Id & EXT_CONTROLLER)
        {
          sendf(appl, _SELECT_B_REQ|CONFIRM, Id, Number, "w", 0x2002); /* wrong controller */
          return 0;
        }
        plci->State=INC_CON_CONNECTED_ALERT;
        plci->appl = appl;
        clear_c_ind_mask_bit (plci, (word)(appl->Id-1));
        dump_c_ind_mask (plci);
        for(i=0; i<max_appl; i++) /* disconnect the other appls */
        {                         /* its quasi a connect        */
          if(test_c_ind_mask_bit (plci, i))
            sendf(&application[i], _DISCONNECT_I, Id, 0, "w", _OTHER_APPL_CONNECTED);
        }
      }

      api_save_msg(msg, "s", &plci->saved_msg);
      tel = plci->tel;
      if(Id & EXT_CONTROLLER)
      {
        if(tel) /* external controller in use by this PLCI */
        {
          if(a->AdvSignalAppl && a->AdvSignalAppl!=appl)
          {
            dbug(1,dprintf("Ext_Ctrl in use 1"));
            Info = _WRONG_STATE;
          }
        }
        else  /* external controller NOT in use by this PLCI ? */
        {
          if(a->AdvSignalPLCI)
          {
            dbug(1,dprintf("Ext_Ctrl in use 2"));
            Info = _WRONG_STATE;
          }
          else /* activate the codec */
          {
            dbug(1,dprintf("Ext_Ctrl start"));
            if(AdvCodecSupport(a, plci, appl, 0) )
            {
              dbug(1,dprintf("Error in codec procedures"));
              Info = _WRONG_STATE;
            }
            else if(plci->spoofed_msg==SPOOFING_REQUIRED) /* wait until codec is active */
            {
              plci->spoofed_msg = AWAITING_SELECT_B;
              plci->internal_command = BLOCK_PLCI; /* lock other commands */
              plci->command = 0;
              dbug(1,dprintf("continue if codec loaded"));
              return false;
            }
          }
        }
      }
      else /* external controller bit is OFF */
      {
        if(tel) /* external controller in use, need to switch off */
        {
          if(a->AdvSignalAppl==appl)
          {
            CodecIdCheck(a, plci);
            plci->tel = 0;
            plci->adv_nl = 0;
            dbug(1,dprintf("Ext_Ctrl disable"));
          }
          else
          {
            dbug(1,dprintf("Ext_Ctrl not requested"));
          }
        }
      }
      if (!Info)
      {
        if (plci->call_dir & CALL_DIR_OUT)
          plci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
        else if (plci->call_dir & CALL_DIR_IN)
          plci->call_dir = CALL_DIR_IN | CALL_DIR_ANSWER;
        start_internal_command (Id, plci, select_b_command);
        return false;
      }
    }
  }
  sendf(appl, _SELECT_B_REQ|CONFIRM, Id, Number, "w", Info);
  return false;
}

static byte manufacturer_req(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			     PLCI *plci, APPL *appl, API_PARSE *parms)
{
  word command;
  word i;
  word ncci;
  API_PARSE * m;
    API_PARSE m_parms[5];
  word codec;
  byte req;
  byte ch;
  byte dir;
  static byte chi[2] = {0x01,0x00};
  static byte lli[2] = {0x01,0x00};
  static byte codec_cai[2] = {0x01,0x01};
  static byte null_msg = {0};
  static API_PARSE null_parms = { 0, &null_msg };
  PLCI   * v_plci;
  word Info=0;

  dbug(1,dprintf("manufacturer_req"));
  for(i=0;i<5;i++) m_parms[i].length = 0;

  if(GET_DWORD(parms[0].info)!=_DI_MANU_ID) {
    Info = _WRONG_MESSAGE_FORMAT;
  }
  command = GET_WORD(parms[1].info);
  m = &parms[2];
  if (!Info)
  {
    switch(command) {
    case _DI_ASSIGN_PLCI:
      if(api_parse(&m->info[1],(word)m->length,"wbbs",m_parms)) {
        Info = _WRONG_MESSAGE_FORMAT;
        break;
      }
      codec = GET_WORD(m_parms[0].info);
      ch = m_parms[1].info[0];
      dir = m_parms[2].info[0];
      if((i=get_plci(a))) {
        plci = &a->plci[i-1];
        plci->appl = appl;
        plci->command = _MANUFACTURER_R;
        plci->m_command = command;
        plci->number = Number;
        plci->State = LOCAL_CONNECT;
        Id = ( ((word)plci->Id<<8)|plci->adapter->Id|0x80);
        dbug(1,dprintf("ManCMD,plci=0x%x",Id));

        if((ch==1 || ch==2) && (dir<=2)) {
          chi[1] = (byte)(0x80|ch);
          lli[1] = 0;
          plci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
          switch(codec)
          {
          case 0:
            Info = add_b1(plci,&m_parms[3],0,0);
            break;
          case 1:
            add_p(plci,CAI,codec_cai);
            break;
          /* manual 'swich on' to the codec support without signalling */
          /* first 'assign plci' with this function, then use */
          case 2:
            if(AdvCodecSupport(a, plci, appl, 0) ) {
              Info = _RESOURCE_ERROR;
            }
            else {
              Info = add_b1(plci,&null_parms,0,B1_FACILITY_LOCAL);
              lli[1] = 0x10; /* local call codec stream */
            }
            break;
          }

          plci->State = LOCAL_CONNECT;
          plci->manufacturer = true;
          plci->command = _MANUFACTURER_R;
          plci->m_command = command;
          plci->number = Number;

          if(!Info)
          {
            add_p(plci,LLI,lli);
            add_p(plci,CHI,chi);
            add_p(plci,UID,"\x06\x43\x61\x70\x69\x32\x30");
            sig_req(plci,ASSIGN,DSIG_ID);

            if(!codec)
            {
              Info = add_b23(plci,&m_parms[3]);
              if(!Info)
              {
                nl_req_ncci(plci,ASSIGN,0);
                send_req(plci);
              }
            }
            if(!Info)
            {
              dbug(1,dprintf("dir=0x%x,spoof=0x%x",dir,plci->spoofed_msg));
              if (plci->spoofed_msg==SPOOFING_REQUIRED)
              {
                api_save_msg (m_parms, "wbbs", &plci->saved_msg);
                plci->spoofed_msg = AWAITING_MANUF_CON;
                plci->internal_command = BLOCK_PLCI; /* reject other req meanwhile */
                plci->command = 0;
                send_req(plci);
                return false;
              }
              if(dir==1) {
                sig_req(plci,CALL_REQ,0);
              }
              else if(!dir){
                sig_req(plci,LISTEN_REQ,0);
              }
              send_req(plci);
            }
            else
            {
              sendf(appl,
                    _MANUFACTURER_R|CONFIRM,
                    Id,
                    Number,
                    "dww",_DI_MANU_ID,command,Info);
              return 2;
            }
          }
        }
      }
      else  Info = _OUT_OF_PLCI;
      break;

    case _DI_IDI_CTRL:
      if(!plci)
      {
        Info = _WRONG_IDENTIFIER;
        break;
      }
      if(api_parse(&m->info[1],(word)m->length,"bs",m_parms)) {
        Info = _WRONG_MESSAGE_FORMAT;
        break;
      }
      req = m_parms[0].info[0];
      plci->command = _MANUFACTURER_R;
      plci->m_command = command;
      plci->number = Number;
      if(req==CALL_REQ)
      {
        plci->b_channel = getChannel(&m_parms[1]);
        mixer_set_bchannel_id_esc (plci, plci->b_channel);
        if(plci->spoofed_msg==SPOOFING_REQUIRED)
        {
          plci->spoofed_msg = CALL_REQ | AWAITING_MANUF_CON;
          plci->internal_command = BLOCK_PLCI; /* reject other req meanwhile */
          plci->command = 0;
          break;
        }
      }
      else if(req==LAW_REQ)
      {
        plci->cr_enquiry = true;
      }
      add_ss(plci,FTY,&m_parms[1]);
      sig_req(plci,req,0);
      send_req(plci);
      if(req==HANGUP)
      {      
        if (plci->NL.Id && !plci->nl_remove_id)
        {
          if (plci->channels)
          {
            for (ncci = 1; ncci < MAX_NCCI+1; ncci++)
            {
              if ((a->ncci_plci[ncci] == plci->Id) && (a->ncci_state[ncci] == CONNECTED))
              {
                a->ncci_state[ncci] = OUTG_DIS_PENDING;
                cleanup_ncci_data (plci, ncci);
                nl_req_ncci(plci,N_DISC,(byte)ncci);
              }
            }
          }
          mixer_remove (plci);
          nl_req_ncci(plci,REMOVE,0);
          send_req(plci);
        }  
      }
      break;

    case _DI_SIG_CTRL:
    /* signalling control for loop activation B-channel */
      if(!plci)
      {
        Info = _WRONG_IDENTIFIER;
        break;
      }
      if(m->length){
        plci->command = _MANUFACTURER_R;
        plci->number = Number;
        add_ss(plci,FTY,m);
        sig_req(plci,SIG_CTRL,0);
        send_req(plci);
      }
      else Info = _WRONG_MESSAGE_FORMAT;
      break;

    case _DI_RXT_CTRL:
    /* activation control for receiver/transmitter B-channel */
      if(!plci)
      {
        Info = _WRONG_IDENTIFIER;
        break;
      }
      if(m->length){
        plci->command = _MANUFACTURER_R;
        plci->number = Number;
        add_ss(plci,FTY,m);
        sig_req(plci,DSP_CTRL,0);
        send_req(plci);
      }
      else Info = _WRONG_MESSAGE_FORMAT;
      break;

    case _DI_ADV_CODEC:
    case _DI_DSP_CTRL:
      /* TEL_CTRL commands to support non standard adjustments: */
      /* Ring on/off, Handset micro volume, external micro vol. */
      /* handset+external speaker volume, receiver+transm. gain,*/
      /* handsfree on (hookinfo off), set mixer command         */

      if(command == _DI_ADV_CODEC)
      {
        if(!a->AdvCodecPLCI) {
          Info = _WRONG_STATE;
          break;
        }
        v_plci = a->AdvCodecPLCI;
      }
      else
      {
        if (plci
         && (m->length >= 3)
         && (m->info[1] == 0x1c)
         && (m->info[2] >= 1))
        {
          if (m->info[3] == DSP_CTRL_OLD_SET_MIXER_COEFFICIENTS)
          {
            if ((plci->tel != ADV_VOICE) || (plci != a->AdvSignalPLCI))
            {
              Info = _WRONG_STATE;
              break;
            }
            a->adv_voice_coef_length = m->info[2] - 1;
            if (a->adv_voice_coef_length > m->length - 3)
              a->adv_voice_coef_length = (byte)(m->length - 3);
            if (a->adv_voice_coef_length > ADV_VOICE_COEF_BUFFER_SIZE)
              a->adv_voice_coef_length = ADV_VOICE_COEF_BUFFER_SIZE;
            for (i = 0; i < a->adv_voice_coef_length; i++)
              a->adv_voice_coef_buffer[i] = m->info[4 + i];
            if (plci->B1_facilities & B1_FACILITY_VOICE)
              adv_voice_write_coefs (plci, ADV_VOICE_WRITE_UPDATE);
            break;
          }
          else if (m->info[3] == DSP_CTRL_SET_DTMF_PARAMETERS)
          {
            if (!(a->manufacturer_features & MANUFACTURER_FEATURE_DTMF_PARAMETERS))
            {
              Info = _FACILITY_NOT_SUPPORTED;
              break;
            }

            plci->dtmf_parameter_length = m->info[2] - 1;
            if (plci->dtmf_parameter_length > m->length - 3)
              plci->dtmf_parameter_length = (byte)(m->length - 3);
            if (plci->dtmf_parameter_length > DTMF_PARAMETER_BUFFER_SIZE)
              plci->dtmf_parameter_length = DTMF_PARAMETER_BUFFER_SIZE;
            for (i = 0; i < plci->dtmf_parameter_length; i++)
              plci->dtmf_parameter_buffer[i] = m->info[4+i];
            if (plci->B1_facilities & B1_FACILITY_DTMFR)
              dtmf_parameter_write (plci);
            break;

          }
        }
        v_plci = plci;
      }

      if(!v_plci)
      {
        Info = _WRONG_IDENTIFIER;
        break;
      }
      if(m->length){
        add_ss(v_plci,FTY,m);
        sig_req(v_plci,TEL_CTRL,0);
        send_req(v_plci);
      }
      else Info = _WRONG_MESSAGE_FORMAT;

      break;

    case _DI_OPTIONS_REQUEST:
      if(api_parse(&m->info[1],(word)m->length,"d",m_parms)) {
        Info = _WRONG_MESSAGE_FORMAT;
        break;
      }
      if (GET_DWORD (m_parms[0].info) & ~a->man_profile.private_options)
      {
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      a->requested_options_table[appl->Id-1] = GET_DWORD (m_parms[0].info);
      break;



    default:
      Info = _WRONG_MESSAGE_FORMAT;
      break;
    }
  }

  sendf(appl,
        _MANUFACTURER_R|CONFIRM,
        Id,
        Number,
        "dww",_DI_MANU_ID,command,Info);
  return false;
}


static byte manufacturer_res(dword Id, word Number, DIVA_CAPI_ADAPTER *a,
			     PLCI *plci, APPL *appl, API_PARSE *msg)
{
  word indication;

    API_PARSE m_parms[3];
  API_PARSE *ncpi;
    API_PARSE fax_parms[9];
  word i;
  byte len;


  dbug(1,dprintf("manufacturer_res"));

  if ((msg[0].length == 0)
   || (msg[1].length == 0)
   || (GET_DWORD(msg[0].info)!=_DI_MANU_ID))
  {
    return false;
  }
  indication = GET_WORD(msg[1].info);
  switch (indication)
  {

  case _DI_NEGOTIATE_B3:
    if(!plci)
      break;
    if (((plci->B3_prot != 4) && (plci->B3_prot != 5))
     || !(plci->ncpi_state & NCPI_NEGOTIATE_B3_SENT))
    {
      dbug(1,dprintf("wrong state for NEGOTIATE_B3 parameters"));
      break;
    }
    if (api_parse (&msg[2].info[1], msg[2].length, "ws", m_parms))
    {
      dbug(1,dprintf("wrong format in NEGOTIATE_B3 parameters"));
      break;
    }
    ncpi = &m_parms[1];
    len = ((byte)(((T30_INFO *) 0)->station_id + 20));
    if (plci->fax_connect_info_length < len)
    {
      ((T30_INFO *)(plci->fax_connect_info_buffer))->station_id_len = 0;
      ((T30_INFO *)(plci->fax_connect_info_buffer))->head_line_len = 0;
    }
    if (api_parse (&ncpi->info[1], ncpi->length, "wwwwssss", fax_parms))
    {
      dbug(1,dprintf("non-standard facilities info missing or wrong format"));
    }
    else
    {
      if (plci->fax_connect_info_length <= len)
        plci->fax_connect_info_buffer[len] = 0;
      len += 1 + plci->fax_connect_info_buffer[len];
      if (plci->fax_connect_info_length <= len)
        plci->fax_connect_info_buffer[len] = 0;
      len += 1 + plci->fax_connect_info_buffer[len];
      if ((fax_parms[7].length >= 3) && (fax_parms[7].info[1] >= 2))
        plci->nsf_control_bits = GET_WORD(&fax_parms[7].info[2]);
      plci->fax_connect_info_buffer[len++] = (byte)(fax_parms[7].length);
      for (i = 0; i < fax_parms[7].length; i++)
        plci->fax_connect_info_buffer[len++] = fax_parms[7].info[1+i];
    }
    plci->fax_connect_info_length = len;
    plci->fax_edata_ack_length = plci->fax_connect_info_length;
    start_internal_command (Id, plci, fax_edata_ack_command);
    break;

  }
  return false;
}

/*------------------------------------------------------------------*/
/* IDI callback function                                            */
/*------------------------------------------------------------------*/

void   callback(ENTITY   * e)
{
  DIVA_CAPI_ADAPTER   * a;
  APPL   * appl;
  PLCI   * plci;
  CAPI_MSG   *m;
  word i, j;
  byte rc;
  byte ch;
  byte req;
  byte global_req;
  int no_cancel_rc;

  dbug(1,dprintf("%x:CB(%x:Req=%x,Rc=%x,Ind=%x)",
                 (e->user[0]+1)&0x7fff,e->Id,e->Req,e->Rc,e->Ind));

  a = &(adapter[(byte)e->user[0]]);
  plci = &(a->plci[e->user[1]]);
  no_cancel_rc = DIVA_CAPI_SUPPORTS_NO_CANCEL(a);

  /*
     If new protocol code and new XDI is used then CAPI should work
     fully in accordance with IDI cpec an look on callback field instead
     of Rc field for return codes.
   */
  if (((e->complete == 0xff) && no_cancel_rc) ||
      (e->Rc && !no_cancel_rc)) {
    rc = e->Rc;
    ch = e->RcCh;
    req = e->Req;
    e->Rc = 0;

    if (e->user[0] & 0x8000)
    {
      /*
         If REMOVE request was sent then we have to wait until
         return code with Id set to zero arrives.
         All other return codes should be ignored.
         */
      if (req == REMOVE)
      {
        if (e->Id)
        {
          dbug(1,dprintf("cancel RC in REMOVE state"));
          return;
        }
        channel_flow_control_remove (plci);
        for (i = 0; i < 256; i++)
        {
          if (a->FlowControlIdTable[i] == plci->nl_remove_id)
            a->FlowControlIdTable[i] = 0;
        }
        plci->nl_remove_id = 0;
        if (plci->rx_dma_descriptor > 0) {
          diva_free_dma_descriptor (plci, plci->rx_dma_descriptor - 1);
          plci->rx_dma_descriptor = 0;
        }
      }
      if (rc == OK_FC)
      {
        a->FlowControlIdTable[ch] = e->Id;
        a->FlowControlSkipTable[ch] = 0;

        a->ch_flow_control[ch] |= N_OK_FC_PENDING;
        a->ch_flow_plci[ch] = plci->Id;
        plci->nl_req = 0;
      }
      else
      {
        /*
          Cancel return codes self, if feature was requested
          */
        if (no_cancel_rc && (a->FlowControlIdTable[ch] == e->Id) && e->Id) {
          a->FlowControlIdTable[ch] = 0;
          if ((rc == OK) && a->FlowControlSkipTable[ch]) {
            dbug(3,dprintf ("XDI CAPI: RC cancelled Id:0x02, Ch:%02x",                              e->Id, ch));
            return;
          }
        }

        if (a->ch_flow_control[ch] & N_OK_FC_PENDING)
        {
          a->ch_flow_control[ch] &= ~N_OK_FC_PENDING;
          if (ch == e->ReqCh)
            plci->nl_req = 0;
        }
        else
          plci->nl_req = 0;
      }
      if (plci->nl_req)
        control_rc (plci, 0, rc, ch, 0, true);
      else
      {
        if (req == N_XON)
        {
          channel_x_on (plci, ch);
          if (plci->internal_command)
            control_rc (plci, req, rc, ch, 0, true);
        }
        else
        {
          if (plci->nl_global_req)
          {
            global_req = plci->nl_global_req;
            plci->nl_global_req = 0;
            if (rc != ASSIGN_OK) {
              e->Id = 0;
              if (plci->rx_dma_descriptor > 0) {
                diva_free_dma_descriptor (plci, plci->rx_dma_descriptor - 1);
                plci->rx_dma_descriptor = 0;
              }
            }
            channel_xmit_xon (plci);
            control_rc (plci, 0, rc, ch, global_req, true);
          }
          else if (plci->data_sent)
          {
            channel_xmit_xon (plci);
            plci->data_sent = false;
            plci->NL.XNum = 1;
            data_rc (plci, ch);
            if (plci->internal_command)
              control_rc (plci, req, rc, ch, 0, true);
          }
          else
          {
            channel_xmit_xon (plci);
            control_rc (plci, req, rc, ch, 0, true);
          }
        }
      }
    }
    else
    {
      /*
         If REMOVE request was sent then we have to wait until
         return code with Id set to zero arrives.
         All other return codes should be ignored.
         */
      if (req == REMOVE)
      {
        if (e->Id)
        {
          dbug(1,dprintf("cancel RC in REMOVE state"));
          return;
        }
        plci->sig_remove_id = 0;
      }
      plci->sig_req = 0;
      if (plci->sig_global_req)
      {
        global_req = plci->sig_global_req;
        plci->sig_global_req = 0;
        if (rc != ASSIGN_OK)
          e->Id = 0;
        channel_xmit_xon (plci);
        control_rc (plci, 0, rc, ch, global_req, false);
      }
      else
      {
        channel_xmit_xon (plci);
        control_rc (plci, req, rc, ch, 0, false);
      }
    }
    /*
      Again: in accordance with IDI spec Rc and Ind can't be delivered in the
      same callback. Also if new XDI and protocol code used then jump
      direct to finish.
      */
    if (no_cancel_rc) {
      channel_xmit_xon(plci);
      goto capi_callback_suffix;
    }
  }

  channel_xmit_xon(plci);

  if (e->Ind) {
    if (e->user[0] &0x8000) {
      byte Ind = e->Ind & 0x0f;
      byte Ch = e->IndCh;
      if (((Ind==N_DISC) || (Ind==N_DISC_ACK)) &&
          (a->ch_flow_plci[Ch] == plci->Id)) {
        if (a->ch_flow_control[Ch] & N_RX_FLOW_CONTROL_MASK) {
          dbug(3,dprintf ("XDI CAPI: I: pending N-XON Ch:%02x", Ch));
        }
        a->ch_flow_control[Ch] &= ~N_RX_FLOW_CONTROL_MASK;
      }
      nl_ind(plci);
      if ((e->RNR != 1) &&
          (a->ch_flow_plci[Ch] == plci->Id) &&
          (a->ch_flow_control[Ch] & N_RX_FLOW_CONTROL_MASK)) {
        a->ch_flow_control[Ch] &= ~N_RX_FLOW_CONTROL_MASK;
        dbug(3,dprintf ("XDI CAPI: I: remove faked N-XON Ch:%02x", Ch));
      }
    } else {
      sig_ind(plci);
    }
    e->Ind = 0;
  }

capi_callback_suffix:

  while (!plci->req_in
   && !plci->internal_command
   && (plci->msg_in_write_pos != plci->msg_in_read_pos))
  {
    j = (plci->msg_in_read_pos == plci->msg_in_wrap_pos) ? 0 : plci->msg_in_read_pos;

    i = (((CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[j]))->header.length + 3) & 0xfffc;

    m = (CAPI_MSG   *)(&((byte   *)(plci->msg_in_queue))[j]);
    appl = *((APPL   *   *)(&((byte   *)(plci->msg_in_queue))[j+i]));
    dbug(1,dprintf("dequeue msg(0x%04x) - write=%d read=%d wrap=%d",
      m->header.command, plci->msg_in_write_pos, plci->msg_in_read_pos, plci->msg_in_wrap_pos));
    if (plci->msg_in_read_pos == plci->msg_in_wrap_pos)
    {
      plci->msg_in_wrap_pos = MSG_IN_QUEUE_SIZE;
      plci->msg_in_read_pos = i + MSG_IN_OVERHEAD;
    }
    else
    {
      plci->msg_in_read_pos = j + i + MSG_IN_OVERHEAD;
    }
    if (plci->msg_in_read_pos == plci->msg_in_write_pos)
    {
      plci->msg_in_write_pos = MSG_IN_QUEUE_SIZE;
      plci->msg_in_read_pos = MSG_IN_QUEUE_SIZE;
    }
    else if (plci->msg_in_read_pos == plci->msg_in_wrap_pos)
    {
      plci->msg_in_read_pos = MSG_IN_QUEUE_SIZE;
      plci->msg_in_wrap_pos = MSG_IN_QUEUE_SIZE;
    }
    i = api_put (appl, m);
    if (i != 0)
    {
      if (m->header.command == _DATA_B3_R)

        TransmitBufferFree (appl, (byte *)(long)(m->info.data_b3_req.Data));

      dbug(1,dprintf("Error 0x%04x from msg(0x%04x)", i, m->header.command));
      break;
    }

    if (plci->li_notify_update)
    {
      plci->li_notify_update = false;
      mixer_notify_update (plci, false);
    }

  }
  send_data(plci);
  send_req(plci);
}


static void control_rc(PLCI *plci, byte req, byte rc, byte ch, byte global_req,
		       byte nl_rc)
{
  dword Id;
  dword rId;
  word Number;
  word Info=0;
  word i;
  word ncci;
  DIVA_CAPI_ADAPTER   * a;
  APPL   * appl;
  PLCI   * rplci;
    byte SSparms[]  = "\x05\x00\x00\x02\x00\x00";
    byte SSstruct[] = "\x09\x00\x00\x06\x00\x00\x00\x00\x00\x00";

  if (!plci) {
    dbug(0,dprintf("A: control_rc, no plci %02x:%02x:%02x:%02x:%02x", req, rc, ch, global_req, nl_rc));
    return;
  }
  dbug(1,dprintf("req0_in/out=%d/%d",plci->req_in,plci->req_out));
  if(plci->req_in!=plci->req_out)
  {
    if (nl_rc || (global_req != ASSIGN) || (rc == ASSIGN_OK))
    {
      dbug(1,dprintf("req_1return"));
      return;
    }
    /* cancel outstanding request on the PLCI after SIG ASSIGN failure */
  }
  plci->req_in = plci->req_in_start = plci->req_out = 0;
  dbug(1,dprintf("control_rc"));

  appl = plci->appl;
  a = plci->adapter;
  ncci = a->ch_ncci[ch];
  if(appl)
  {
    Id = (((dword)(ncci ? ncci : ch)) << 16) | ((word)plci->Id << 8) | a->Id;
    if(plci->tel && plci->SuppState!=CALL_HELD) Id|=EXT_CONTROLLER;
    Number = plci->number;
    dbug(1,dprintf("Contr_RC-Id=%08lx,plci=%x,tel=%x, entity=0x%x, command=0x%x, int_command=0x%x",Id,plci->Id,plci->tel,plci->Sig.Id,plci->command,plci->internal_command));
    dbug(1,dprintf("channels=0x%x",plci->channels));
    if (plci_remove_check(plci))
      return;
    if(req==REMOVE && rc==ASSIGN_OK)
    {
      sig_req(plci,HANGUP,0);
      sig_req(plci,REMOVE,0);
      send_req(plci);
    }
    if(plci->command)
    {
      switch(plci->command)
      {
      case C_HOLD_REQ:
        dbug(1,dprintf("HoldRC=0x%x",rc));
        SSparms[1] = (byte)S_HOLD;
        if(rc!=OK)
        {
          plci->SuppState = IDLE;
          Info = 0x2001;
        }
        sendf(appl,_FACILITY_R|CONFIRM,Id,Number,"wws",Info,3,SSparms);
        break;

      case C_RETRIEVE_REQ:
        dbug(1,dprintf("RetrieveRC=0x%x",rc));
        SSparms[1] = (byte)S_RETRIEVE;
        if(rc!=OK)
        {
          plci->SuppState = CALL_HELD;
          Info = 0x2001;
        }
        sendf(appl,_FACILITY_R|CONFIRM,Id,Number,"wws",Info,3,SSparms);
        break;

      case _INFO_R:
        dbug(1,dprintf("InfoRC=0x%x",rc));
        if(rc!=OK) Info=_WRONG_STATE;
        sendf(appl,_INFO_R|CONFIRM,Id,Number,"w",Info);
        break;

      case _CONNECT_R:
        dbug(1,dprintf("Connect_R=0x%x/0x%x/0x%x/0x%x",req,rc,global_req,nl_rc));
        if (plci->State == INC_DIS_PENDING)
          break;
        if(plci->Sig.Id!=0xff)
        {
          if (((global_req == ASSIGN) && (rc != ASSIGN_OK))
           || (!nl_rc && (req == CALL_REQ) && (rc != OK)))
          {
            dbug(1,dprintf("No more IDs/Call_Req failed"));
            sendf(appl,_CONNECT_R|CONFIRM,Id&0xffL,Number,"w",_OUT_OF_PLCI);
            plci_remove(plci);
            plci->State = IDLE;
            break;
          }
          if(plci->State!=LOCAL_CONNECT)plci->State = OUTG_CON_PENDING;
          sendf(appl,_CONNECT_R|CONFIRM,Id,Number,"w",0);
        }
        else /* D-ch activation */
        {
          if (rc != ASSIGN_OK)
          {
            dbug(1,dprintf("No more IDs/X.25 Call_Req failed"));
            sendf(appl,_CONNECT_R|CONFIRM,Id&0xffL,Number,"w",_OUT_OF_PLCI);
            plci_remove(plci);
            plci->State = IDLE;
            break;
          }
          sendf(appl,_CONNECT_R|CONFIRM,Id,Number,"w",0);
          sendf(plci->appl,_CONNECT_ACTIVE_I,Id,0,"sss","","","");
          plci->State = INC_ACT_PENDING;
        }
        break;

      case _CONNECT_I|RESPONSE:
        if (plci->State != INC_DIS_PENDING)
          plci->State = INC_CON_ACCEPT;
        break;

      case _DISCONNECT_R:
        if (plci->State == INC_DIS_PENDING)
          break;
        if(plci->Sig.Id!=0xff)
        {
          plci->State = OUTG_DIS_PENDING;
          sendf(appl,_DISCONNECT_R|CONFIRM,Id,Number,"w",0);
        }
        break;

      case SUSPEND_REQ:
        break;

      case RESUME_REQ:
        break;

      case _CONNECT_B3_R:
        if(rc!=OK)
        {
          sendf(appl,_CONNECT_B3_R|CONFIRM,Id,Number,"w",_WRONG_IDENTIFIER);
          break;
        }
        ncci = get_ncci (plci, ch, 0);
        Id = (Id & 0xffff) | (((dword) ncci) << 16);
        plci->channels++;
        if(req==N_RESET)
        {
          a->ncci_state[ncci] = INC_ACT_PENDING;
          sendf(appl,_CONNECT_B3_R|CONFIRM,Id,Number,"w",0);
          sendf(appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
        }
        else
        {
          a->ncci_state[ncci] = OUTG_CON_PENDING;
          sendf(appl,_CONNECT_B3_R|CONFIRM,Id,Number,"w",0);
        }
        break;

      case _CONNECT_B3_I|RESPONSE:
        break;

      case _RESET_B3_R:
/*        sendf(appl,_RESET_B3_R|CONFIRM,Id,Number,"w",0);*/
        break;

      case _DISCONNECT_B3_R:
        sendf(appl,_DISCONNECT_B3_R|CONFIRM,Id,Number,"w",0);
        break;

      case _MANUFACTURER_R:
        break;

      case PERM_LIST_REQ:
        if(rc!=OK)
        {
          Info = _WRONG_IDENTIFIER;
          sendf(plci->appl,_CONNECT_R|CONFIRM,Id,Number,"w",Info);
          plci_remove(plci);
        }
        else
          sendf(plci->appl,_CONNECT_R|CONFIRM,Id,Number,"w",Info);
        break;

      default:
        break;
      }
      plci->command = 0;
    }
    else if (plci->internal_command)
    {
      switch(plci->internal_command)
      {
      case BLOCK_PLCI:
        return;

      case GET_MWI_STATE:
        if(rc==OK) /* command supported, wait for indication */
        {
          return;
        }
        plci_remove(plci);
        break;

        /* Get Supported Services */
      case GETSERV_REQ_PEND:
        if(rc==OK) /* command supported, wait for indication */
        {
          break;
        }
        PUT_DWORD(&SSstruct[6], MASK_TERMINAL_PORTABILITY);
        sendf(appl, _FACILITY_R|CONFIRM, Id, Number, "wws",0,3,SSstruct);
        plci_remove(plci);
        break;

      case INTERR_DIVERSION_REQ_PEND:      /* Interrogate Parameters        */
      case INTERR_NUMBERS_REQ_PEND:
      case CF_START_PEND:                  /* Call Forwarding Start pending */
      case CF_STOP_PEND:                   /* Call Forwarding Stop pending  */
      case CCBS_REQUEST_REQ_PEND:
      case CCBS_DEACTIVATE_REQ_PEND:
      case CCBS_INTERROGATE_REQ_PEND:
        switch(plci->internal_command)
        {
          case INTERR_DIVERSION_REQ_PEND:
            SSparms[1] = S_INTERROGATE_DIVERSION;
            break;
          case INTERR_NUMBERS_REQ_PEND:
            SSparms[1] = S_INTERROGATE_NUMBERS;
            break;
          case CF_START_PEND:
            SSparms[1] = S_CALL_FORWARDING_START;
            break;
          case CF_STOP_PEND:
            SSparms[1] = S_CALL_FORWARDING_STOP;
            break;
          case CCBS_REQUEST_REQ_PEND:
            SSparms[1] = S_CCBS_REQUEST;
            break;
          case CCBS_DEACTIVATE_REQ_PEND:
            SSparms[1] = S_CCBS_DEACTIVATE;
            break;
          case CCBS_INTERROGATE_REQ_PEND:
            SSparms[1] = S_CCBS_INTERROGATE;
            break;
        }
        if(global_req==ASSIGN)
        {
          dbug(1,dprintf("AssignDiversion_RC=0x%x/0x%x",req,rc));
          return;
        }
        if(!plci->appl) break;
        if(rc==ISDN_GUARD_REJ)
        {
          Info = _CAPI_GUARD_ERROR;
        }
        else if(rc!=OK)
        {
          Info = _SUPPLEMENTARY_SERVICE_NOT_SUPPORTED;
        }
        sendf(plci->appl,_FACILITY_R|CONFIRM,Id&0x7,
              plci->number,"wws",Info,(word)3,SSparms);
        if(Info) plci_remove(plci);
        break;

        /* 3pty conference pending */
      case PTY_REQ_PEND:
        if(!plci->relatedPTYPLCI) break;
        rplci = plci->relatedPTYPLCI;
        SSparms[1] = plci->ptyState;
        rId = ((word)rplci->Id<<8)|rplci->adapter->Id;
        if(rplci->tel) rId|=EXT_CONTROLLER;
        if(rc!=OK)
        {
          Info = 0x300E; /* not supported */
          plci->relatedPTYPLCI = NULL;
          plci->ptyState = 0;
        }
        sendf(rplci->appl,
              _FACILITY_R|CONFIRM,
              rId,
              plci->number,
              "wws",Info,(word)3,SSparms);
        break;

        /* Explicit Call Transfer pending */
      case ECT_REQ_PEND:
        dbug(1,dprintf("ECT_RC=0x%x/0x%x",req,rc));
        if(!plci->relatedPTYPLCI) break;
        rplci = plci->relatedPTYPLCI;
        SSparms[1] = S_ECT;
        rId = ((word)rplci->Id<<8)|rplci->adapter->Id;
        if(rplci->tel) rId|=EXT_CONTROLLER;
        if(rc!=OK)
        {
          Info = 0x300E; /* not supported */
          plci->relatedPTYPLCI = NULL;
          plci->ptyState = 0;
        }
        sendf(rplci->appl,
              _FACILITY_R|CONFIRM,
              rId,
              plci->number,
              "wws",Info,(word)3,SSparms);
        break;

      case _MANUFACTURER_R:
        dbug(1,dprintf("_Manufacturer_R=0x%x/0x%x",req,rc));
        if ((global_req == ASSIGN) && (rc != ASSIGN_OK))
        {
          dbug(1,dprintf("No more IDs"));
          sendf(appl,_MANUFACTURER_R|CONFIRM,Id,Number,"dww",_DI_MANU_ID,_MANUFACTURER_R,_OUT_OF_PLCI);
          plci_remove(plci);  /* after codec init, internal codec commands pending */
        }
        break;

      case _CONNECT_R:
        dbug(1,dprintf("_Connect_R=0x%x/0x%x",req,rc));
        if ((global_req == ASSIGN) && (rc != ASSIGN_OK))
        {
          dbug(1,dprintf("No more IDs"));
          sendf(appl,_CONNECT_R|CONFIRM,Id&0xffL,Number,"w",_OUT_OF_PLCI);
          plci_remove(plci);  /* after codec init, internal codec commands pending */
        }
        break;

      case PERM_COD_HOOK:                     /* finished with Hook_Ind */
        return;

      case PERM_COD_CALL:
        dbug(1,dprintf("***Codec Connect_Pending A, Rc = 0x%x",rc));
        plci->internal_command = PERM_COD_CONN_PEND;
        return;

      case PERM_COD_ASSIGN:
        dbug(1,dprintf("***Codec Assign A, Rc = 0x%x",rc));
        if(rc!=ASSIGN_OK) break;
        sig_req(plci,CALL_REQ,0);
        send_req(plci);
        plci->internal_command = PERM_COD_CALL;
        return;

        /* Null Call Reference Request pending */
      case C_NCR_FAC_REQ:
        dbug(1,dprintf("NCR_FAC=0x%x/0x%x",req,rc));
        if(global_req==ASSIGN)
        {
          if(rc==ASSIGN_OK)
          {
            return;
          }
          else
          {
            sendf(appl,_INFO_R|CONFIRM,Id&0xf,Number,"w",_WRONG_STATE);
            appl->NullCREnable = false;
            plci_remove(plci);
          }
        }
        else if(req==NCR_FACILITY)
        {
          if(rc==OK)
          {
            sendf(appl,_INFO_R|CONFIRM,Id&0xf,Number,"w",0);
          }
          else
          {
            sendf(appl,_INFO_R|CONFIRM,Id&0xf,Number,"w",_WRONG_STATE);
            appl->NullCREnable = false;
          }
          plci_remove(plci);
        }
        break;

      case HOOK_ON_REQ:
        if(plci->channels)
        {
          if(a->ncci_state[ncci]==CONNECTED)
          {
            a->ncci_state[ncci] = OUTG_DIS_PENDING;
            cleanup_ncci_data (plci, ncci);
            nl_req_ncci(plci,N_DISC,(byte)ncci);
          }
          break;
        }
        break;

      case HOOK_OFF_REQ:
        if (plci->State == INC_DIS_PENDING)
          break;
        sig_req(plci,CALL_REQ,0);
        send_req(plci);
        plci->State=OUTG_CON_PENDING;
        break;


      case MWI_ACTIVATE_REQ_PEND:
      case MWI_DEACTIVATE_REQ_PEND:
        if(global_req == ASSIGN && rc==ASSIGN_OK)
        {
          dbug(1,dprintf("MWI_REQ assigned"));
          return;
        }
        else if(rc!=OK)
        {                 
          if(rc==WRONG_IE)
          {
            Info = 0x2007; /* Illegal message parameter coding */
            dbug(1,dprintf("MWI_REQ invalid parameter"));
          }
          else
          {
            Info = 0x300B; /* not supported */                      
            dbug(1,dprintf("MWI_REQ not supported"));
          }
          /* 0x3010: Request not allowed in this state */
          PUT_WORD(&SSparms[4],0x300E); /* SS not supported */
                    
        }
        if(plci->internal_command==MWI_ACTIVATE_REQ_PEND)
        {
          PUT_WORD(&SSparms[1],S_MWI_ACTIVATE);
        }
        else PUT_WORD(&SSparms[1],S_MWI_DEACTIVATE);

        if(plci->cr_enquiry)
        {
          sendf(plci->appl,
                _FACILITY_R|CONFIRM,
                Id&0xf,
                plci->number,
                "wws",Info,(word)3,SSparms);
          if(rc!=OK) plci_remove(plci);
        }
        else
        {
          sendf(plci->appl,
                _FACILITY_R|CONFIRM,
                Id,
                plci->number,
                "wws",Info,(word)3,SSparms);
        }
        break;

      case CONF_BEGIN_REQ_PEND:
      case CONF_ADD_REQ_PEND:
      case CONF_SPLIT_REQ_PEND:
      case CONF_DROP_REQ_PEND:
      case CONF_ISOLATE_REQ_PEND:
      case CONF_REATTACH_REQ_PEND:
        dbug(1,dprintf("CONF_RC=0x%x/0x%x",req,rc));
        if((plci->internal_command==CONF_ADD_REQ_PEND)&&(!plci->relatedPTYPLCI)) break;
        rplci = plci;
        rId = Id;
        switch(plci->internal_command)
        {
          case CONF_BEGIN_REQ_PEND:
            SSparms[1] = S_CONF_BEGIN;
            break;
          case CONF_ADD_REQ_PEND:
            SSparms[1] = S_CONF_ADD;
            rplci = plci->relatedPTYPLCI;
            rId = ((word)rplci->Id<<8)|rplci->adapter->Id;
            break;
          case CONF_SPLIT_REQ_PEND:
            SSparms[1] = S_CONF_SPLIT;
            break;
          case CONF_DROP_REQ_PEND:
            SSparms[1] = S_CONF_DROP;
            break;
          case CONF_ISOLATE_REQ_PEND:
            SSparms[1] = S_CONF_ISOLATE;
            break;
          case CONF_REATTACH_REQ_PEND:
            SSparms[1] = S_CONF_REATTACH;
            break;
        }
        
        if(rc!=OK)
        {
          Info = 0x300E; /* not supported */
          plci->relatedPTYPLCI = NULL;
          plci->ptyState = 0;
        }
        sendf(rplci->appl,
              _FACILITY_R|CONFIRM,
              rId,
              plci->number,
              "wws",Info,(word)3,SSparms);
        break;

      case VSWITCH_REQ_PEND:
        if(rc!=OK)
        {
          if(plci->relatedPTYPLCI)
          {
            plci->relatedPTYPLCI->vswitchstate=0;
            plci->relatedPTYPLCI->vsprot=0;
            plci->relatedPTYPLCI->vsprotdialect=0;    
          }
          plci->vswitchstate=0;
          plci->vsprot=0;
          plci->vsprotdialect=0;
        }
        else
        {
          if(plci->relatedPTYPLCI &&
             plci->vswitchstate==1 &&
             plci->relatedPTYPLCI->vswitchstate==3) /* join complete */
            plci->vswitchstate=3;
        }
        break;

  /* Call Deflection Request pending (SSCT) */
      case CD_REQ_PEND:
        SSparms[1] = S_CALL_DEFLECTION;
        if(rc!=OK)
        {
          Info = 0x300E; /* not supported */
          plci->appl->CDEnable = 0;
        }  
        sendf(plci->appl,_FACILITY_R|CONFIRM,Id,
          plci->number,"wws",Info,(word)3,SSparms);
        break;

      case RTP_CONNECT_B3_REQ_COMMAND_2:
        if (rc == OK)
        {
          ncci = get_ncci (plci, ch, 0);
          Id = (Id & 0xffff) | (((dword) ncci) << 16);
          plci->channels++;
          a->ncci_state[ncci] = OUTG_CON_PENDING;
        }

      default:
        if (plci->internal_command_queue[0])
        {
          (*(plci->internal_command_queue[0]))(Id, plci, rc);
          if (plci->internal_command)
            return;
        }
        break;
      }
      next_internal_command (Id, plci);
    }
  }
  else /* appl==0 */
  {
    Id = ((word)plci->Id<<8)|plci->adapter->Id;
    if(plci->tel) Id|=EXT_CONTROLLER;

    switch(plci->internal_command)
    {
    case BLOCK_PLCI:
      return;

    case START_L1_SIG_ASSIGN_PEND:
    case REM_L1_SIG_ASSIGN_PEND:
      if(global_req == ASSIGN)
      {
        break;
      }
      else
      {
        dbug(1,dprintf("***L1 Req rem PLCI"));
        plci->internal_command = 0;
        sig_req(plci,REMOVE,0);
        send_req(plci);
      }
      break;

      /* Call Deflection Request pending, just no appl ptr assigned */
    case CD_REQ_PEND:
      SSparms[1] = S_CALL_DEFLECTION;
      if(rc!=OK)
      {
        Info = 0x300E; /* not supported */
      }
      for(i=0; i<max_appl; i++)
      {
        if(application[i].CDEnable)
        {
          if(!application[i].Id) application[i].CDEnable = 0;
          else
          {
            sendf(&application[i],_FACILITY_R|CONFIRM,Id,
                  plci->number,"wws",Info,(word)3,SSparms);
            if(Info) application[i].CDEnable = 0;
          }
        }
      }
      plci->internal_command = 0;
      break;

    case PERM_COD_HOOK:                   /* finished with Hook_Ind */
      return;

    case PERM_COD_CALL:
      plci->internal_command = PERM_COD_CONN_PEND;
      dbug(1,dprintf("***Codec Connect_Pending, Rc = 0x%x",rc));
      return;

    case PERM_COD_ASSIGN:
      dbug(1,dprintf("***Codec Assign, Rc = 0x%x",rc));
      plci->internal_command = 0;
      if(rc!=ASSIGN_OK) break;
      plci->internal_command = PERM_COD_CALL;
      sig_req(plci,CALL_REQ,0);
      send_req(plci);
      return;

    case LISTEN_SIG_ASSIGN_PEND:
      if(rc == ASSIGN_OK)
      {
        plci->internal_command = 0;
        dbug(1,dprintf("ListenCheck, new SIG_ID = 0x%x",plci->Sig.Id));
        add_p(plci,ESC,"\x02\x18\x00");             /* support call waiting */
        sig_req(plci,INDICATE_REQ,0);
        send_req(plci);
      }
      else
      {
        dbug(1,dprintf("ListenCheck failed (assignRc=0x%x)",rc));
        a->listen_active--;
        plci_remove(plci);
        plci->State = IDLE;
      }
      break;

    case USELAW_REQ:
      if(global_req == ASSIGN)
      {
        if (rc==ASSIGN_OK)
      {
        sig_req(plci,LAW_REQ,0);
        send_req(plci);
        dbug(1,dprintf("Auto-Law assigned"));
        }
        else
        {
          dbug(1,dprintf("Auto-Law assign failed"));
          a->automatic_law = 3;
          plci->internal_command = 0;
          a->automatic_lawPLCI = NULL;
        }
        break;
      }
      else if(req == LAW_REQ && rc==OK)
      {
        dbug(1,dprintf("Auto-Law initiated"));
        a->automatic_law = 2;
        plci->internal_command = 0;
      }
      else
      {
        dbug(1,dprintf("Auto-Law not supported"));
        a->automatic_law = 3;
        plci->internal_command = 0;
        sig_req(plci,REMOVE,0);
        send_req(plci);
        a->automatic_lawPLCI = NULL;
      }
      break;
    }
    plci_remove_check(plci);
  }
}

static void data_rc(PLCI *plci, byte ch)
{
  dword Id;
  DIVA_CAPI_ADAPTER   * a;
  NCCI   *ncci_ptr;
  DATA_B3_DESC   *data;
  word ncci;

  if (plci->appl)
  {
    TransmitBufferFree (plci->appl, plci->data_sent_ptr);
    a = plci->adapter;
    ncci = a->ch_ncci[ch];
    if (ncci && (a->ncci_plci[ncci] == plci->Id))
    {
      ncci_ptr = &(a->ncci[ncci]);
      dbug(1,dprintf("data_out=%d, data_pending=%d",ncci_ptr->data_out,ncci_ptr->data_pending));
      if (ncci_ptr->data_pending)
      {
        data = &(ncci_ptr->DBuffer[ncci_ptr->data_out]);
        if (!(data->Flags &4) && a->ncci_state[ncci])
        {
          Id = (((dword)ncci)<<16)|((word)plci->Id<<8)|a->Id;
          if(plci->tel) Id|=EXT_CONTROLLER;
          sendf(plci->appl,_DATA_B3_R|CONFIRM,Id,data->Number,
                "ww",data->Handle,0);
        }
        (ncci_ptr->data_out)++;
        if (ncci_ptr->data_out == MAX_DATA_B3)
          ncci_ptr->data_out = 0;
        (ncci_ptr->data_pending)--;
      }
    }
  }
}

static void data_ack(PLCI *plci, byte ch)
{
  dword Id;
  DIVA_CAPI_ADAPTER   * a;
  NCCI   *ncci_ptr;
  word ncci;

  a = plci->adapter;
  ncci = a->ch_ncci[ch];
  ncci_ptr = &(a->ncci[ncci]);
  if (ncci_ptr->data_ack_pending)
  {
    if (a->ncci_state[ncci] && (a->ncci_plci[ncci] == plci->Id))
    {
      Id = (((dword)ncci)<<16)|((word)plci->Id<<8)|a->Id;
      if(plci->tel) Id|=EXT_CONTROLLER;
      sendf(plci->appl,_DATA_B3_R|CONFIRM,Id,ncci_ptr->DataAck[ncci_ptr->data_ack_out].Number,
            "ww",ncci_ptr->DataAck[ncci_ptr->data_ack_out].Handle,0);
    }
    (ncci_ptr->data_ack_out)++;
    if (ncci_ptr->data_ack_out == MAX_DATA_ACK)
      ncci_ptr->data_ack_out = 0;
    (ncci_ptr->data_ack_pending)--;
  }
}

static void sig_ind(PLCI *plci)
{
  dword x_Id;
  dword Id;
  dword rId;
  word Number = 0;
  word i;
  word cip;
  dword cip_mask;
  byte   *ie;
  DIVA_CAPI_ADAPTER   * a;
    API_PARSE saved_parms[MAX_MSG_PARMS+1];
#define MAXPARMSIDS 31
    byte   * parms[MAXPARMSIDS];
    byte   * add_i[4];
    byte   * multi_fac_parms[MAX_MULTI_IE];
    byte   * multi_pi_parms [MAX_MULTI_IE];
    byte   * multi_ssext_parms [MAX_MULTI_IE];
    byte   * multi_CiPN_parms [MAX_MULTI_IE];

    byte   * multi_vswitch_parms [MAX_MULTI_IE];

  byte ai_len;
    byte   *esc_chi = "";
    byte   *esc_law = "";
    byte   *pty_cai = "";
    byte   *esc_cr  = "";
    byte   *esc_profile = "";

    byte facility[256];
  PLCI   * tplci = NULL;
  byte chi[] = "\x02\x18\x01";
  byte voice_cai[]  = "\x06\x14\x00\x00\x00\x00\x08";
    byte resume_cau[] = "\x05\x05\x00\x02\x00\x00";
  /* ESC_MSGTYPE must be the last but one message, a new IE has to be */
  /* included before the ESC_MSGTYPE and MAXPARMSIDS has to be incremented */
  /* SMSG is situated at the end because its 0 (for compatibility reasons */
  /* (see Info_Mask Bit 4, first IE. then the message type)           */
    word parms_id[] =
         {MAXPARMSIDS, CPN, 0xff, DSA, OSA, BC, LLC, HLC, ESC_CAUSE, DSP, DT, CHA,
          UUI, CONG_RR, CONG_RNR, ESC_CHI, KEY, CHI, CAU, ESC_LAW,
          RDN, RDX, CONN_NR, RIN, NI, CAI, ESC_CR,
          CST, ESC_PROFILE, 0xff, ESC_MSGTYPE, SMSG};
          /* 14 FTY repl by ESC_CHI */
          /* 18 PI  repl by ESC_LAW */
         /* removed OAD changed to 0xff for future use, OAD is multiIE now */
     word multi_fac_id[] = {1, FTY};
     word multi_pi_id[]  = {1, PI};
     word multi_CiPN_id[]  = {1, OAD};
     word multi_ssext_id[]  = {1, ESC_SSEXT};

     word multi_vswitch_id[]  = {1, ESC_VSWITCH};

  byte   * cau;
  word ncci;
    byte SS_Ind[] = "\x05\x02\x00\x02\x00\x00"; /* Hold_Ind struct*/
    byte CF_Ind[] = "\x09\x02\x00\x06\x00\x00\x00\x00\x00\x00";
    byte Interr_Err_Ind[] = "\x0a\x02\x00\x07\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    byte CONF_Ind[] = "\x09\x16\x00\x06\x00\x00\0x00\0x00\0x00\0x00";
  byte force_mt_info = false;
  byte dir;
  dword d;
  word w;

  a = plci->adapter;
  Id = ((word)plci->Id<<8)|a->Id;
  PUT_WORD(&SS_Ind[4],0x0000);

  if (plci->sig_remove_id)
  {
    plci->Sig.RNR = 2; /* discard */
    dbug(1,dprintf("SIG discard while remove pending"));
    return;
  }
  if(plci->tel && plci->SuppState!=CALL_HELD) Id|=EXT_CONTROLLER;
  dbug(1,dprintf("SigInd-Id=%08lx,plci=%x,tel=%x,state=0x%x,channels=%d,Discflowcl=%d",
    Id,plci->Id,plci->tel,plci->State,plci->channels,plci->hangup_flow_ctrl_timer));
  if(plci->Sig.Ind==CALL_HOLD_ACK && plci->channels)
  {
    plci->Sig.RNR = 1;
    return;
  }
  if(plci->Sig.Ind==HANGUP && plci->channels)
  {
    plci->Sig.RNR = 1;
    plci->hangup_flow_ctrl_timer++;
    /* recover the network layer after timeout */
    if(plci->hangup_flow_ctrl_timer==100)
    {
      dbug(1,dprintf("Exceptional disc"));
      plci->Sig.RNR = 0;
      plci->hangup_flow_ctrl_timer = 0;
      for (ncci = 1; ncci < MAX_NCCI+1; ncci++)
      {
        if (a->ncci_plci[ncci] == plci->Id)
        {
          cleanup_ncci_data (plci, ncci);
          if(plci->channels)plci->channels--;
          if (plci->appl)
            sendf(plci->appl,_DISCONNECT_B3_I, (((dword) ncci) << 16) | Id,0,"ws",0,"");
        }
      }
      if (plci->appl)
        sendf(plci->appl, _DISCONNECT_I, Id, 0, "w", 0);
      plci_remove(plci);
      plci->State=IDLE;
    }
    return;
  }

  /* do first parse the info with no OAD in, because OAD will be converted */
  /* first the multiple facility IE, then mult. progress ind.              */
  /* then the parameters for the info_ind + conn_ind                       */
  IndParse(plci,multi_fac_id,multi_fac_parms,MAX_MULTI_IE);
  IndParse(plci,multi_pi_id,multi_pi_parms,MAX_MULTI_IE);
  IndParse(plci,multi_ssext_id,multi_ssext_parms,MAX_MULTI_IE);

  IndParse(plci,multi_vswitch_id,multi_vswitch_parms,MAX_MULTI_IE);

  IndParse(plci,parms_id,parms,0);
  IndParse(plci,multi_CiPN_id,multi_CiPN_parms,MAX_MULTI_IE);
  esc_chi  = parms[14];
  esc_law  = parms[18];
  pty_cai  = parms[24];
  esc_cr   = parms[25];
  esc_profile = parms[27];
  if(esc_cr[0] && plci)
  {
    if(plci->cr_enquiry && plci->appl)
    {
      plci->cr_enquiry = false;
      /* d = MANU_ID            */
      /* w = m_command          */
      /* b = total length       */
      /* b = indication type    */
      /* b = length of all IEs  */
      /* b = IE1                */
      /* S = IE1 length + cont. */
      /* b = IE2                */
      /* S = IE2 length + cont. */
      sendf(plci->appl,
        _MANUFACTURER_I,
        Id,
        0,
        "dwbbbbSbS",_DI_MANU_ID,plci->m_command,
        2+1+1+esc_cr[0]+1+1+esc_law[0],plci->Sig.Ind,1+1+esc_cr[0]+1+1+esc_law[0],ESC,esc_cr,ESC,esc_law);
    }
  }
  /* create the additional info structure                                  */
  add_i[1] = parms[15]; /* KEY of additional info */
  add_i[2] = parms[11]; /* UUI of additional info */
  ai_len = AddInfo(add_i,multi_fac_parms, esc_chi, facility);

  /* the ESC_LAW indicates if u-Law or a-Law is actually used by the card  */
  /* indication returns by the card if requested by the function           */
  /* AutomaticLaw() after driver init                                      */
  if (a->automatic_law<4)
  {
    if(esc_law[0]){
      if(esc_law[2]){
        dbug(0,dprintf("u-Law selected"));
        a->u_law = 1;
      }
      else {
        dbug(0,dprintf("a-Law selected"));
        a->u_law = 0;
      }
      a->automatic_law = 4;
      if(plci==a->automatic_lawPLCI) {
        plci->internal_command = 0;
        sig_req(plci,REMOVE,0);
        send_req(plci);
        a->automatic_lawPLCI = NULL;
      }
    }
    if (esc_profile[0])
    {
      dbug (1, dprintf ("[%06x] CardProfile: %lx %lx %lx %lx %lx",
        UnMapController (a->Id), GET_DWORD (&esc_profile[6]),
        GET_DWORD (&esc_profile[10]), GET_DWORD (&esc_profile[14]),
        GET_DWORD (&esc_profile[18]), GET_DWORD (&esc_profile[46])));

      a->profile.Global_Options &= 0x000000ffL;
      a->profile.B1_Protocols &= 0x000003ffL;
      a->profile.B2_Protocols &= 0x00001fdfL;
      a->profile.B3_Protocols &= 0x000000b7L;

      a->profile.Global_Options &= GET_DWORD (&esc_profile[6]) |
        GL_BCHANNEL_OPERATION_SUPPORTED;
      a->profile.B1_Protocols &= GET_DWORD (&esc_profile[10]);
      a->profile.B2_Protocols &= GET_DWORD (&esc_profile[14]);
      a->profile.B3_Protocols &= GET_DWORD (&esc_profile[18]);
      a->manufacturer_features = GET_DWORD (&esc_profile[46]);
      a->man_profile.private_options = 0;

      if (a->manufacturer_features & MANUFACTURER_FEATURE_ECHO_CANCELLER)
      {
        a->man_profile.private_options |= 1L << PRIVATE_ECHO_CANCELLER;
        a->profile.Global_Options |= GL_ECHO_CANCELLER_SUPPORTED;
      }


      if (a->manufacturer_features & MANUFACTURER_FEATURE_RTP)
        a->man_profile.private_options |= 1L << PRIVATE_RTP;
      a->man_profile.rtp_primary_payloads = GET_DWORD (&esc_profile[50]);
      a->man_profile.rtp_additional_payloads = GET_DWORD (&esc_profile[54]);


      if (a->manufacturer_features & MANUFACTURER_FEATURE_T38)
        a->man_profile.private_options |= 1L << PRIVATE_T38;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_FAX_SUB_SEP_PWD)
        a->man_profile.private_options |= 1L << PRIVATE_FAX_SUB_SEP_PWD;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_V18)
        a->man_profile.private_options |= 1L << PRIVATE_V18;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_DTMF_TONE)
        a->man_profile.private_options |= 1L << PRIVATE_DTMF_TONE;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_PIAFS)
        a->man_profile.private_options |= 1L << PRIVATE_PIAFS;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_FAX_PAPER_FORMATS)
        a->man_profile.private_options |= 1L << PRIVATE_FAX_PAPER_FORMATS;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_VOWN)
        a->man_profile.private_options |= 1L << PRIVATE_VOWN;


      if (a->manufacturer_features & MANUFACTURER_FEATURE_FAX_NONSTANDARD)
        a->man_profile.private_options |= 1L << PRIVATE_FAX_NONSTANDARD;

    }
    else
    {
      a->profile.Global_Options &= 0x0000007fL;
      a->profile.B1_Protocols &= 0x000003dfL;
      a->profile.B2_Protocols &= 0x00001adfL;
      a->profile.B3_Protocols &= 0x000000b7L;
      a->manufacturer_features &= MANUFACTURER_FEATURE_HARDDTMF;
    }
    if (a->manufacturer_features & (MANUFACTURER_FEATURE_HARDDTMF |
      MANUFACTURER_FEATURE_SOFTDTMF_SEND | MANUFACTURER_FEATURE_SOFTDTMF_RECEIVE))
    {
      a->profile.Global_Options |= GL_DTMF_SUPPORTED;
    }
    a->manufacturer_features &= ~MANUFACTURER_FEATURE_OOB_CHANNEL;
    dbug (1, dprintf ("[%06x] Profile: %lx %lx %lx %lx %lx",
      UnMapController (a->Id), a->profile.Global_Options,
      a->profile.B1_Protocols, a->profile.B2_Protocols,
      a->profile.B3_Protocols, a->manufacturer_features));
  }
  /* codec plci for the handset/hook state support is just an internal id  */
  if(plci!=a->AdvCodecPLCI)
  {
    force_mt_info =  SendMultiIE(plci,Id,multi_fac_parms, FTY, 0x20, 0);
    force_mt_info |= SendMultiIE(plci,Id,multi_pi_parms, PI, 0x210, 0);
    SendSSExtInd(NULL,plci,Id,multi_ssext_parms);
    SendInfo(plci,Id, parms, force_mt_info);

    VSwitchReqInd(plci,Id,multi_vswitch_parms);

  }

  /* switch the codec to the b-channel                                     */
  if(esc_chi[0] && plci && !plci->SuppState){
    plci->b_channel = esc_chi[esc_chi[0]]&0x1f;
    mixer_set_bchannel_id_esc (plci, plci->b_channel);
    dbug(1,dprintf("storeChannel=0x%x",plci->b_channel));
    if(plci->tel==ADV_VOICE && plci->appl) {
      SetVoiceChannel(a->AdvCodecPLCI, esc_chi, a);
    }
  }

  if(plci->appl) Number = plci->appl->Number++;

  switch(plci->Sig.Ind) {
  /* Response to Get_Supported_Services request */
  case S_SUPPORTED:
    dbug(1,dprintf("S_Supported"));
    if(!plci->appl) break;
    if(pty_cai[0]==4)
    {
      PUT_DWORD(&CF_Ind[6],GET_DWORD(&pty_cai[1]) );
    }
    else
    {
      PUT_DWORD(&CF_Ind[6],MASK_TERMINAL_PORTABILITY | MASK_HOLD_RETRIEVE);
    }
    PUT_WORD (&CF_Ind[1], 0);
    PUT_WORD (&CF_Ind[4], 0);
    sendf(plci->appl,_FACILITY_R|CONFIRM,Id&0x7,plci->number, "wws",0,3,CF_Ind);
    plci_remove(plci);
    break;
                    
  /* Supplementary Service rejected */
  case S_SERVICE_REJ:
    dbug(1,dprintf("S_Reject=0x%x",pty_cai[5]));
    if(!pty_cai[0]) break;
    switch (pty_cai[5])
    {
    case ECT_EXECUTE:
    case THREE_PTY_END:
    case THREE_PTY_BEGIN:
      if(!plci->relatedPTYPLCI) break;
      tplci = plci->relatedPTYPLCI;
      rId = ( (word)tplci->Id<<8)|tplci->adapter->Id;
      if(tplci->tel) rId|=EXT_CONTROLLER;
      if(pty_cai[5]==ECT_EXECUTE)
      {
        PUT_WORD(&SS_Ind[1],S_ECT);

        plci->vswitchstate=0;
        plci->relatedPTYPLCI->vswitchstate=0;

      }
      else
      {
        PUT_WORD(&SS_Ind[1],pty_cai[5]+3);
      }
      if(pty_cai[2]!=0xff)
      {
        PUT_WORD(&SS_Ind[4],0x3600|(word)pty_cai[2]);
      }
      else
      {
        PUT_WORD(&SS_Ind[4],0x300E);
      }
      plci->relatedPTYPLCI = NULL;
      plci->ptyState = 0;
      sendf(tplci->appl,_FACILITY_I,rId,0,"ws",3, SS_Ind);
      break;

    case CALL_DEFLECTION:
      if(pty_cai[2]!=0xff)
      {
        PUT_WORD(&SS_Ind[4],0x3600|(word)pty_cai[2]);
      }
      else
      {
        PUT_WORD(&SS_Ind[4],0x300E);
      }
      PUT_WORD(&SS_Ind[1],pty_cai[5]);
      for(i=0; i<max_appl; i++)
      {
        if(application[i].CDEnable)
        {
          if(application[i].Id) sendf(&application[i],_FACILITY_I,Id,0,"ws",3, SS_Ind);
          application[i].CDEnable = false;
        }
      }
      break;

    case DEACTIVATION_DIVERSION:
    case ACTIVATION_DIVERSION:
    case DIVERSION_INTERROGATE_CFU:
    case DIVERSION_INTERROGATE_CFB:
    case DIVERSION_INTERROGATE_CFNR:
    case DIVERSION_INTERROGATE_NUM:
    case CCBS_REQUEST:
    case CCBS_DEACTIVATE:
    case CCBS_INTERROGATE:
      if(!plci->appl) break;
      if(pty_cai[2]!=0xff)
      {
        PUT_WORD(&Interr_Err_Ind[4],0x3600|(word)pty_cai[2]);
      }
      else
      {
        PUT_WORD(&Interr_Err_Ind[4],0x300E);
      }
      switch (pty_cai[5])
      {
        case DEACTIVATION_DIVERSION:
          dbug(1,dprintf("Deact_Div"));
          Interr_Err_Ind[0]=0x9;
          Interr_Err_Ind[3]=0x6;
          PUT_WORD(&Interr_Err_Ind[1],S_CALL_FORWARDING_STOP);
          break;
        case ACTIVATION_DIVERSION:
          dbug(1,dprintf("Act_Div"));
          Interr_Err_Ind[0]=0x9;
          Interr_Err_Ind[3]=0x6;
          PUT_WORD(&Interr_Err_Ind[1],S_CALL_FORWARDING_START);
          break;
        case DIVERSION_INTERROGATE_CFU:
        case DIVERSION_INTERROGATE_CFB:
        case DIVERSION_INTERROGATE_CFNR:
          dbug(1,dprintf("Interr_Div"));
          Interr_Err_Ind[0]=0xa;
          Interr_Err_Ind[3]=0x7;
          PUT_WORD(&Interr_Err_Ind[1],S_INTERROGATE_DIVERSION);
          break;
        case DIVERSION_INTERROGATE_NUM:
          dbug(1,dprintf("Interr_Num"));
          Interr_Err_Ind[0]=0xa;
          Interr_Err_Ind[3]=0x7;
          PUT_WORD(&Interr_Err_Ind[1],S_INTERROGATE_NUMBERS);
          break;
        case CCBS_REQUEST:
          dbug(1,dprintf("CCBS Request"));
          Interr_Err_Ind[0]=0xd;
          Interr_Err_Ind[3]=0xa;
          PUT_WORD(&Interr_Err_Ind[1],S_CCBS_REQUEST);
          break;
        case CCBS_DEACTIVATE:
          dbug(1,dprintf("CCBS Deactivate"));
          Interr_Err_Ind[0]=0x9;
          Interr_Err_Ind[3]=0x6;
          PUT_WORD(&Interr_Err_Ind[1],S_CCBS_DEACTIVATE);
          break;
        case CCBS_INTERROGATE:
          dbug(1,dprintf("CCBS Interrogate"));
          Interr_Err_Ind[0]=0xb;
          Interr_Err_Ind[3]=0x8;
          PUT_WORD(&Interr_Err_Ind[1],S_CCBS_INTERROGATE);
          break;
      }
      PUT_DWORD(&Interr_Err_Ind[6],plci->appl->S_Handle);
      sendf(plci->appl,_FACILITY_I,Id&0x7,0,"ws",3, Interr_Err_Ind);
      plci_remove(plci);
      break;
    case ACTIVATION_MWI:      
    case DEACTIVATION_MWI:
      if(pty_cai[5]==ACTIVATION_MWI)
      {
        PUT_WORD(&SS_Ind[1],S_MWI_ACTIVATE);
      }
      else PUT_WORD(&SS_Ind[1],S_MWI_DEACTIVATE);
      
      if(pty_cai[2]!=0xff)
      {
        PUT_WORD(&SS_Ind[4],0x3600|(word)pty_cai[2]);
      }
      else
      {
        PUT_WORD(&SS_Ind[4],0x300E);
      }

      if(plci->cr_enquiry)
      {
        sendf(plci->appl,_FACILITY_I,Id&0xf,0,"ws",3, SS_Ind);
        plci_remove(plci);
      }
      else
      {
        sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, SS_Ind);
      }
      break;
    case CONF_ADD: /* ERROR */
    case CONF_BEGIN:
    case CONF_DROP:
    case CONF_ISOLATE:
    case CONF_REATTACH:
      CONF_Ind[0]=9;
      CONF_Ind[3]=6;   
      switch(pty_cai[5])
      {
      case CONF_BEGIN:
          PUT_WORD(&CONF_Ind[1],S_CONF_BEGIN);
          plci->ptyState = 0;
          break;
      case CONF_DROP:
          CONF_Ind[0]=5;
          CONF_Ind[3]=2;
          PUT_WORD(&CONF_Ind[1],S_CONF_DROP);
          plci->ptyState = CONNECTED;
          break;
      case CONF_ISOLATE:
          CONF_Ind[0]=5;
          CONF_Ind[3]=2;
          PUT_WORD(&CONF_Ind[1],S_CONF_ISOLATE);
          plci->ptyState = CONNECTED;
          break;
      case CONF_REATTACH:
          CONF_Ind[0]=5;
          CONF_Ind[3]=2;
          PUT_WORD(&CONF_Ind[1],S_CONF_REATTACH);
          plci->ptyState = CONNECTED;
          break;
      case CONF_ADD:
          PUT_WORD(&CONF_Ind[1],S_CONF_ADD);
          plci->relatedPTYPLCI = NULL;
          tplci=plci->relatedPTYPLCI;
          if(tplci) tplci->ptyState = CONNECTED;
          plci->ptyState = CONNECTED;
          break;
      }
          
      if(pty_cai[2]!=0xff)
      {
        PUT_WORD(&CONF_Ind[4],0x3600|(word)pty_cai[2]);
      }
      else
      {
        PUT_WORD(&CONF_Ind[4],0x3303); /* Time-out: network did not respond
                                            within the required time */
      }

      PUT_DWORD(&CONF_Ind[6],0x0);
      sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, CONF_Ind);
      break;
    }
    break;

  /* Supplementary Service indicates success */
  case S_SERVICE:
    dbug(1,dprintf("Service_Ind"));
    PUT_WORD (&CF_Ind[4], 0);
    switch (pty_cai[5])
    {
    case THREE_PTY_END:
    case THREE_PTY_BEGIN:
    case ECT_EXECUTE:
      if(!plci->relatedPTYPLCI) break;
      tplci = plci->relatedPTYPLCI;
      rId = ( (word)tplci->Id<<8)|tplci->adapter->Id;
      if(tplci->tel) rId|=EXT_CONTROLLER;
      if(pty_cai[5]==ECT_EXECUTE)
      {
        PUT_WORD(&SS_Ind[1],S_ECT);

        if(plci->vswitchstate!=3)
        {

        plci->ptyState = IDLE;
        plci->relatedPTYPLCI = NULL;
        plci->ptyState = 0;

        }

        dbug(1,dprintf("ECT OK"));
        sendf(tplci->appl,_FACILITY_I,rId,0,"ws",3, SS_Ind);



      }
      else
      {
        switch (plci->ptyState)
        {
        case S_3PTY_BEGIN:
          plci->ptyState = CONNECTED;
          dbug(1,dprintf("3PTY ON"));
          break;

        case S_3PTY_END:
          plci->ptyState = IDLE;
          plci->relatedPTYPLCI = NULL;
          plci->ptyState = 0;
          dbug(1,dprintf("3PTY OFF"));
          break;
        }
        PUT_WORD(&SS_Ind[1],pty_cai[5]+3);
        sendf(tplci->appl,_FACILITY_I,rId,0,"ws",3, SS_Ind);
      }
      break;

    case CALL_DEFLECTION:
      PUT_WORD(&SS_Ind[1],pty_cai[5]);
      for(i=0; i<max_appl; i++)
      {
        if(application[i].CDEnable)
        {
          if(application[i].Id) sendf(&application[i],_FACILITY_I,Id,0,"ws",3, SS_Ind);
          application[i].CDEnable = false;
        }
      }
      break;

    case DEACTIVATION_DIVERSION:
    case ACTIVATION_DIVERSION:
      if(!plci->appl) break;
      PUT_WORD(&CF_Ind[1],pty_cai[5]+2);
      PUT_DWORD(&CF_Ind[6],plci->appl->S_Handle);
      sendf(plci->appl,_FACILITY_I,Id&0x7,0,"ws",3, CF_Ind);
      plci_remove(plci);
      break;

    case DIVERSION_INTERROGATE_CFU:
    case DIVERSION_INTERROGATE_CFB:
    case DIVERSION_INTERROGATE_CFNR:
    case DIVERSION_INTERROGATE_NUM:
    case CCBS_REQUEST:
    case CCBS_DEACTIVATE:
    case CCBS_INTERROGATE:
      if(!plci->appl) break;
      switch (pty_cai[5])
      {
        case DIVERSION_INTERROGATE_CFU:
        case DIVERSION_INTERROGATE_CFB:
        case DIVERSION_INTERROGATE_CFNR:
          dbug(1,dprintf("Interr_Div"));
          PUT_WORD(&pty_cai[1],S_INTERROGATE_DIVERSION);
          pty_cai[3]=pty_cai[0]-3; /* Supplementary Service-specific parameter len */
          break;
        case DIVERSION_INTERROGATE_NUM:
          dbug(1,dprintf("Interr_Num"));
          PUT_WORD(&pty_cai[1],S_INTERROGATE_NUMBERS);
          pty_cai[3]=pty_cai[0]-3; /* Supplementary Service-specific parameter len */
          break;
        case CCBS_REQUEST:
          dbug(1,dprintf("CCBS Request"));
          PUT_WORD(&pty_cai[1],S_CCBS_REQUEST);
          pty_cai[3]=pty_cai[0]-3; /* Supplementary Service-specific parameter len */
          break;
        case CCBS_DEACTIVATE:
          dbug(1,dprintf("CCBS Deactivate"));
          PUT_WORD(&pty_cai[1],S_CCBS_DEACTIVATE);
          pty_cai[3]=pty_cai[0]-3; /* Supplementary Service-specific parameter len */
          break;
        case CCBS_INTERROGATE:
          dbug(1,dprintf("CCBS Interrogate"));
          PUT_WORD(&pty_cai[1],S_CCBS_INTERROGATE);
          pty_cai[3]=pty_cai[0]-3; /* Supplementary Service-specific parameter len */
          break;
      }
      PUT_WORD(&pty_cai[4],0); /* Supplementary Service Reason */
      PUT_DWORD(&pty_cai[6],plci->appl->S_Handle);
      sendf(plci->appl,_FACILITY_I,Id&0x7,0,"wS",3, pty_cai);
      plci_remove(plci);
      break;

    case ACTIVATION_MWI:
    case DEACTIVATION_MWI:
      if(pty_cai[5]==ACTIVATION_MWI)
      {
        PUT_WORD(&SS_Ind[1],S_MWI_ACTIVATE);
      }
      else PUT_WORD(&SS_Ind[1],S_MWI_DEACTIVATE);
      if(plci->cr_enquiry)
      {
        sendf(plci->appl,_FACILITY_I,Id&0xf,0,"ws",3, SS_Ind);
        plci_remove(plci);
      }
      else
      {
        sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, SS_Ind);
      }
      break;
    case MWI_INDICATION:
      if(pty_cai[0]>=0x12)
      {
        PUT_WORD(&pty_cai[3],S_MWI_INDICATE);
        pty_cai[2]=pty_cai[0]-2; /* len Parameter */
        pty_cai[5]=pty_cai[0]-5; /* Supplementary Service-specific parameter len */
        if(plci->appl && (a->Notification_Mask[plci->appl->Id-1]&SMASK_MWI))
        {
          if(plci->internal_command==GET_MWI_STATE) /* result on Message Waiting Listen */
          {
            sendf(plci->appl,_FACILITY_I,Id&0xf,0,"wS",3, &pty_cai[2]);
            plci_remove(plci);
            return;
          }
          else  sendf(plci->appl,_FACILITY_I,Id,0,"wS",3, &pty_cai[2]);
          pty_cai[0]=0;
        }
        else
        {
          for(i=0; i<max_appl; i++)
          {                     
            if(a->Notification_Mask[i]&SMASK_MWI)
            {
              sendf(&application[i],_FACILITY_I,Id&0x7,0,"wS",3, &pty_cai[2]);
              pty_cai[0]=0;
            }
          }
        }

        if(!pty_cai[0])
        { /* acknowledge */
          facility[2]= 0; /* returncode */
        }
        else facility[2]= 0xff;
      }
      else
      {
        /* reject */
        facility[2]= 0xff; /* returncode */
      }
      facility[0]= 2;
      facility[1]= MWI_RESPONSE; /* Function */
      add_p(plci,CAI,facility);
      add_p(plci,ESC,multi_ssext_parms[0]); /* remembered parameter -> only one possible */
      sig_req(plci,S_SERVICE,0);
      send_req(plci);
      plci->command = 0;
      next_internal_command (Id, plci);
      break;
    case CONF_ADD: /* OK */
    case CONF_BEGIN:
    case CONF_DROP:
    case CONF_ISOLATE:
    case CONF_REATTACH:
    case CONF_PARTYDISC:
      CONF_Ind[0]=9;
      CONF_Ind[3]=6;
      switch(pty_cai[5])
      {
      case CONF_BEGIN:
          PUT_WORD(&CONF_Ind[1],S_CONF_BEGIN);
          if(pty_cai[0]==6)
          {
              d=pty_cai[6];
              PUT_DWORD(&CONF_Ind[6],d); /* PartyID */
          }
          else
          {
              PUT_DWORD(&CONF_Ind[6],0x0);
          }
          break;
      case CONF_ISOLATE:
          PUT_WORD(&CONF_Ind[1],S_CONF_ISOLATE);
          CONF_Ind[0]=5;
          CONF_Ind[3]=2;
          break;
      case CONF_REATTACH:
          PUT_WORD(&CONF_Ind[1],S_CONF_REATTACH);
          CONF_Ind[0]=5;
          CONF_Ind[3]=2;
          break;
      case CONF_DROP:
          PUT_WORD(&CONF_Ind[1],S_CONF_DROP);
          CONF_Ind[0]=5;
          CONF_Ind[3]=2;
          break;
      case CONF_ADD:
          PUT_WORD(&CONF_Ind[1],S_CONF_ADD);
          d=pty_cai[6];
          PUT_DWORD(&CONF_Ind[6],d); /* PartyID */
          tplci=plci->relatedPTYPLCI;
          if(tplci) tplci->ptyState = CONNECTED;
          break;
      case CONF_PARTYDISC:
          CONF_Ind[0]=7;
          CONF_Ind[3]=4;          
          PUT_WORD(&CONF_Ind[1],S_CONF_PARTYDISC);
          d=pty_cai[6];
          PUT_DWORD(&CONF_Ind[4],d); /* PartyID */
          break;
      }
      plci->ptyState = CONNECTED;
      sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, CONF_Ind);
      break;
    case CCBS_INFO_RETAIN:
    case CCBS_ERASECALLLINKAGEID:
    case CCBS_STOP_ALERTING:
      CONF_Ind[0]=5;
      CONF_Ind[3]=2;
      switch(pty_cai[5])
      {
      case CCBS_INFO_RETAIN:
        PUT_WORD(&CONF_Ind[1],S_CCBS_INFO_RETAIN);
        break;
      case CCBS_STOP_ALERTING:
        PUT_WORD(&CONF_Ind[1],S_CCBS_STOP_ALERTING);
    break;
      case CCBS_ERASECALLLINKAGEID:
        PUT_WORD(&CONF_Ind[1],S_CCBS_ERASECALLLINKAGEID);
        CONF_Ind[0]=7;
        CONF_Ind[3]=4;
        CONF_Ind[6]=0;
        CONF_Ind[7]=0;
        break;
      }      
      w=pty_cai[6];
      PUT_WORD(&CONF_Ind[4],w); /* PartyID */

      if(plci->appl && (a->Notification_Mask[plci->appl->Id-1]&SMASK_CCBS))
      {
        sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, CONF_Ind);
      }
      else
      {
        for(i=0; i<max_appl; i++)
            if(a->Notification_Mask[i]&SMASK_CCBS)
                sendf(&application[i],_FACILITY_I,Id&0x7,0,"ws",3, CONF_Ind);
      }
      break;
    }
    break;
  case CALL_HOLD_REJ:
    cau = parms[7];
    if(cau)
    {
      i = _L3_CAUSE | cau[2];
      if(cau[2]==0) i = 0x3603;
    }
    else
    {
      i = 0x3603;
    }
    PUT_WORD(&SS_Ind[1],S_HOLD);
    PUT_WORD(&SS_Ind[4],i);
    if(plci->SuppState == HOLD_REQUEST)
    {
      plci->SuppState = IDLE;
      sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, SS_Ind);
    }
    break;

  case CALL_HOLD_ACK:
    if(plci->SuppState == HOLD_REQUEST)
    {
      plci->SuppState = CALL_HELD;
      CodecIdCheck(a, plci);
      start_internal_command (Id, plci, hold_save_command);
    }
    break;

  case CALL_RETRIEVE_REJ:
    cau = parms[7];
    if(cau)
    {
      i = _L3_CAUSE | cau[2];
      if(cau[2]==0) i = 0x3603;
    }
    else
    {
      i = 0x3603;
    }
    PUT_WORD(&SS_Ind[1],S_RETRIEVE);
    PUT_WORD(&SS_Ind[4],i);
    if(plci->SuppState == RETRIEVE_REQUEST)
    {
      plci->SuppState = CALL_HELD;
      CodecIdCheck(a, plci);
      sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, SS_Ind);
    }
    break;

  case CALL_RETRIEVE_ACK:
    PUT_WORD(&SS_Ind[1],S_RETRIEVE);
    if(plci->SuppState == RETRIEVE_REQUEST)
    {
      plci->SuppState = IDLE;
      plci->call_dir |= CALL_DIR_FORCE_OUTG_NL;
      plci->b_channel = esc_chi[esc_chi[0]]&0x1f;
      if(plci->tel)
      {
        mixer_set_bchannel_id_esc (plci, plci->b_channel);
        dbug(1,dprintf("RetrChannel=0x%x",plci->b_channel));
        SetVoiceChannel(a->AdvCodecPLCI, esc_chi, a);
        if(plci->B2_prot==B2_TRANSPARENT && plci->B3_prot==B3_TRANSPARENT)
        {
          dbug(1,dprintf("Get B-ch"));
          start_internal_command (Id, plci, retrieve_restore_command);
        }
        else
          sendf(plci->appl,_FACILITY_I,Id,0,"ws",3, SS_Ind);
      }
      else
        start_internal_command (Id, plci, retrieve_restore_command);
    }
    break;

  case INDICATE_IND:
    if(plci->State != LISTENING) {
      sig_req(plci,HANGUP,0);
      send_req(plci);
      break;
    }
    cip = find_cip(a,parms[4],parms[6]);
    cip_mask = 1L<<cip;
    dbug(1,dprintf("cip=%d,cip_mask=%lx",cip,cip_mask));
    clear_c_ind_mask (plci);
    if (!remove_started && !a->adapter_disabled)
    {
      set_c_ind_mask_bit (plci, MAX_APPL);
      group_optimization(a, plci);
      for(i=0; i<max_appl; i++) {
        if(application[i].Id
        && (a->CIP_Mask[i]&1 || a->CIP_Mask[i]&cip_mask)
        && CPN_filter_ok(parms[0],a,i)
        && test_group_ind_mask_bit (plci, i) ) {
          dbug(1,dprintf("storedcip_mask[%d]=0x%lx",i,a->CIP_Mask[i] ));
          set_c_ind_mask_bit (plci, i);
          dump_c_ind_mask (plci);
          plci->State = INC_CON_PENDING;
          plci->call_dir = (plci->call_dir & ~(CALL_DIR_OUT | CALL_DIR_ORIGINATE)) |
            CALL_DIR_IN | CALL_DIR_ANSWER;
          if(esc_chi[0]) {
            plci->b_channel = esc_chi[esc_chi[0]]&0x1f;
            mixer_set_bchannel_id_esc (plci, plci->b_channel);
          }
          /* if a listen on the ext controller is done, check if hook states */
          /* are supported or if just a on board codec must be activated     */
          if(a->codec_listen[i] && !a->AdvSignalPLCI) {
            if(a->profile.Global_Options & HANDSET)
              plci->tel = ADV_VOICE;
            else if(a->profile.Global_Options & ON_BOARD_CODEC)
              plci->tel = CODEC;
            if(plci->tel) Id|=EXT_CONTROLLER;
            a->codec_listen[i] = plci;
          }

          sendf(&application[i],_CONNECT_I,Id,0,
                "wSSSSSSSbSSSSS", cip,    /* CIP                 */
                             parms[0],    /* CalledPartyNumber   */
                             multi_CiPN_parms[0],    /* CallingPartyNumber  */
                             parms[2],    /* CalledPartySubad    */
                             parms[3],    /* CallingPartySubad   */
                             parms[4],    /* BearerCapability    */
                             parms[5],    /* LowLC               */
                             parms[6],    /* HighLC              */
                             ai_len,      /* nested struct add_i */
                             add_i[0],    /* B channel info    */
                             add_i[1],    /* keypad facility   */
                             add_i[2],    /* user user data    */
                             add_i[3],    /* nested facility   */
                             multi_CiPN_parms[1]    /* second CiPN(SCR)   */
                             );
          SendSSExtInd(&application[i],
                        plci,
                        Id,
                        multi_ssext_parms);
          SendSetupInfo(&application[i],
                        plci,
                        Id,
                        parms,
                        SendMultiIE(plci,Id,multi_pi_parms, PI, 0x210, true));
        }
      }
      clear_c_ind_mask_bit (plci, MAX_APPL);
      dump_c_ind_mask (plci);
    }
    if(c_ind_mask_empty (plci)) {
      sig_req(plci,HANGUP,0);
      send_req(plci);
      plci->State = IDLE;
    }
    plci->notifiedcall = 0;
    a->listen_active--;
    listen_check(a);
    break;

  case CALL_PEND_NOTIFY:
    plci->notifiedcall = 1;
    listen_check(a);
    break;

  case CALL_IND:
  case CALL_CON:
    if(plci->State==ADVANCED_VOICE_SIG || plci->State==ADVANCED_VOICE_NOSIG)
    {
      if(plci->internal_command==PERM_COD_CONN_PEND)
      {
        if(plci->State==ADVANCED_VOICE_NOSIG)
        {
          dbug(1,dprintf("***Codec OK"));
          if(a->AdvSignalPLCI)
          {
            tplci = a->AdvSignalPLCI;
            if(tplci->spoofed_msg)
            {
              dbug(1,dprintf("***Spoofed Msg(0x%x)",tplci->spoofed_msg));
              tplci->command = 0;
              tplci->internal_command = 0;
              x_Id = ((word)tplci->Id<<8)|tplci->adapter->Id | 0x80;
              switch (tplci->spoofed_msg)
              {
              case CALL_RES:
                tplci->command = _CONNECT_I|RESPONSE;
                api_load_msg (&tplci->saved_msg, saved_parms);
                add_b1(tplci,&saved_parms[1],0,tplci->B1_facilities);
                if (tplci->adapter->Info_Mask[tplci->appl->Id-1] & 0x200)
                {
                  /* early B3 connect (CIP mask bit 9) no release after a disc */
                  add_p(tplci,LLI,"\x01\x01");
                }
                add_s(tplci, CONN_NR, &saved_parms[2]);
                add_s(tplci, LLC, &saved_parms[4]);
                add_ai(tplci, &saved_parms[5]);
                tplci->State = INC_CON_ACCEPT;
                sig_req(tplci, CALL_RES,0);
                send_req(tplci);
                break;

              case AWAITING_SELECT_B:
                dbug(1,dprintf("Select_B continue"));
                start_internal_command (x_Id, tplci, select_b_command);
                break;

              case AWAITING_MANUF_CON: /* Get_Plci per Manufacturer_Req to ext controller */
                if(!tplci->Sig.Id)
                {
                  dbug(1,dprintf("No SigID!"));
                  sendf(tplci->appl, _MANUFACTURER_R|CONFIRM,x_Id,tplci->number, "dww",_DI_MANU_ID,_MANUFACTURER_R,_OUT_OF_PLCI);
                  plci_remove(tplci);
                  break;
                }
                tplci->command = _MANUFACTURER_R;
                api_load_msg (&tplci->saved_msg, saved_parms);
                dir = saved_parms[2].info[0];
                if(dir==1) {
                  sig_req(tplci,CALL_REQ,0);
                }
                else if(!dir){
                  sig_req(tplci,LISTEN_REQ,0);
                }
                send_req(tplci);
                sendf(tplci->appl, _MANUFACTURER_R|CONFIRM,x_Id,tplci->number, "dww",_DI_MANU_ID,_MANUFACTURER_R,0);
                break;

              case (CALL_REQ|AWAITING_MANUF_CON):
                sig_req(tplci,CALL_REQ,0);
                send_req(tplci);
                break;

              case CALL_REQ:
                if(!tplci->Sig.Id)
                {
                  dbug(1,dprintf("No SigID!"));
                  sendf(tplci->appl,_CONNECT_R|CONFIRM,tplci->adapter->Id,0,"w",_OUT_OF_PLCI);
                  plci_remove(tplci);
                  break;
                }
                tplci->command = _CONNECT_R;
                api_load_msg (&tplci->saved_msg, saved_parms);
                add_s(tplci,CPN,&saved_parms[1]);
                add_s(tplci,DSA,&saved_parms[3]);
                add_ai(tplci,&saved_parms[9]);
                sig_req(tplci,CALL_REQ,0);
                send_req(tplci);
                break;

              case CALL_RETRIEVE:
                tplci->command = C_RETRIEVE_REQ;
                sig_req(tplci,CALL_RETRIEVE,0);
                send_req(tplci);
                break;
              }
              tplci->spoofed_msg = 0;
              if (tplci->internal_command == 0)
                next_internal_command (x_Id, tplci);
            }
          }
          next_internal_command (Id, plci);
          break;
        }
        dbug(1,dprintf("***Codec Hook Init Req"));
        plci->internal_command = PERM_COD_HOOK;
        add_p(plci,FTY,"\x01\x09");             /* Get Hook State*/
        sig_req(plci,TEL_CTRL,0);
        send_req(plci);
      }
    }
    else if(plci->command != _MANUFACTURER_R  /* old style permanent connect */
    && plci->State!=INC_ACT_PENDING)
    {
      mixer_set_bchannel_id_esc (plci, plci->b_channel);
      if(plci->tel == ADV_VOICE && plci->SuppState == IDLE) /* with permanent codec switch on immediately */
      {
        chi[2] = plci->b_channel;
        SetVoiceChannel(a->AdvCodecPLCI, chi, a);
      }
      sendf(plci->appl,_CONNECT_ACTIVE_I,Id,0,"Sss",parms[21],"","");
      plci->State = INC_ACT_PENDING;
    }
    break;

  case TEL_CTRL:
    Number = 0;
    ie = multi_fac_parms[0]; /* inspect the facility hook indications */
    if(plci->State==ADVANCED_VOICE_SIG && ie[0]){
      switch (ie[1]&0x91) {
        case 0x80:   /* hook off */
        case 0x81:
          if(plci->internal_command==PERM_COD_HOOK)
          {
            dbug(1,dprintf("init:hook_off"));
            plci->hook_state = ie[1];
            next_internal_command (Id, plci);
            break;
          }
          else /* ignore doubled hook indications */
          {
            if( ((plci->hook_state)&0xf0)==0x80)
            {
              dbug(1,dprintf("ignore hook"));
              break;
            }
            plci->hook_state = ie[1]&0x91;
          }
          /* check for incoming call pending */
          /* and signal '+'.Appl must decide */
          /* with connect_res if call must   */
          /* accepted or not                 */
          for(i=0, tplci=NULL;i<max_appl;i++){
            if(a->codec_listen[i]
            && (a->codec_listen[i]->State==INC_CON_PENDING
              ||a->codec_listen[i]->State==INC_CON_ALERT) ){
              tplci = a->codec_listen[i];
              tplci->appl = &application[i];
            }
          }
          /* no incoming call, do outgoing call */
          /* and signal '+' if outg. setup   */
          if(!a->AdvSignalPLCI && !tplci){
            if((i=get_plci(a))) {
              a->AdvSignalPLCI = &a->plci[i-1];
              tplci = a->AdvSignalPLCI;
              tplci->tel  = ADV_VOICE;
              PUT_WORD(&voice_cai[5],a->AdvSignalAppl->MaxDataLength);
              if (a->Info_Mask[a->AdvSignalAppl->Id-1] & 0x200){
                /* early B3 connect (CIP mask bit 9) no release after a disc */
                add_p(tplci,LLI,"\x01\x01");
              }
              add_p(tplci, CAI, voice_cai);
              add_p(tplci, OAD, a->TelOAD);
              add_p(tplci, OSA, a->TelOSA);
              add_p(tplci,SHIFT|6,NULL);
              add_p(tplci,SIN,"\x02\x01\x00");
              add_p(tplci,UID,"\x06\x43\x61\x70\x69\x32\x30");
              sig_req(tplci,ASSIGN,DSIG_ID);
              a->AdvSignalPLCI->internal_command = HOOK_OFF_REQ;
              a->AdvSignalPLCI->command = 0;
              tplci->appl = a->AdvSignalAppl;
              tplci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
              send_req(tplci);
            }

          }

          if(!tplci) break;
          Id = ((word)tplci->Id<<8)|a->Id;
          Id|=EXT_CONTROLLER;
          sendf(tplci->appl,
                _FACILITY_I,
                Id,
                0,
                "ws", (word)0, "\x01+");
          break;

        case 0x90:   /* hook on  */
        case 0x91:
          if(plci->internal_command==PERM_COD_HOOK)
          {
            dbug(1,dprintf("init:hook_on"));
            plci->hook_state = ie[1]&0x91;
            next_internal_command (Id, plci);
            break;
          }
          else /* ignore doubled hook indications */
          {
            if( ((plci->hook_state)&0xf0)==0x90) break;
            plci->hook_state = ie[1]&0x91;
          }
          /* hangup the adv. voice call and signal '-' to the appl */
          if(a->AdvSignalPLCI) {
            Id = ((word)a->AdvSignalPLCI->Id<<8)|a->Id;
            if(plci->tel) Id|=EXT_CONTROLLER;
            sendf(a->AdvSignalAppl,
                  _FACILITY_I,
                  Id,
                  0,
                  "ws", (word)0, "\x01-");
            a->AdvSignalPLCI->internal_command = HOOK_ON_REQ;
            a->AdvSignalPLCI->command = 0;
            sig_req(a->AdvSignalPLCI,HANGUP,0);
            send_req(a->AdvSignalPLCI);
          }
          break;
      }
    }
    break;

  case RESUME:
    clear_c_ind_mask_bit (plci, (word)(plci->appl->Id-1));
    PUT_WORD(&resume_cau[4],GOOD);
    sendf(plci->appl,_FACILITY_I,Id,0,"ws", (word)3, resume_cau);
    break;

  case SUSPEND:
    clear_c_ind_mask (plci);

    if (plci->NL.Id && !plci->nl_remove_id) {
      mixer_remove (plci);
      nl_req_ncci(plci,REMOVE,0);
    }
    if (!plci->sig_remove_id) {
      plci->internal_command = 0;
      sig_req(plci,REMOVE,0);
    }
    send_req(plci);
    if(!plci->channels) {
      sendf(plci->appl,_FACILITY_I,Id,0,"ws", (word)3, "\x05\x04\x00\x02\x00\x00");
      sendf(plci->appl, _DISCONNECT_I, Id, 0, "w", 0);
    }
    break;

  case SUSPEND_REJ:
    break;

  case HANGUP:
    plci->hangup_flow_ctrl_timer=0;
    if(plci->manufacturer && plci->State==LOCAL_CONNECT) break;
    cau = parms[7];
    if(cau) {
      i = _L3_CAUSE | cau[2];
      if(cau[2]==0) i = 0;
      else if(cau[2]==8) i = _L1_ERROR;
      else if(cau[2]==9 || cau[2]==10) i = _L2_ERROR;
      else if(cau[2]==5) i = _CAPI_GUARD_ERROR;
    }
    else {
      i = _L3_ERROR;
    }

    if(plci->State==INC_CON_PENDING || plci->State==INC_CON_ALERT)
    {
      for(i=0; i<max_appl; i++)
      {
        if(test_c_ind_mask_bit (plci, i))
          sendf(&application[i], _DISCONNECT_I, Id, 0, "w", 0);
      }
    }
    else
    {
      clear_c_ind_mask (plci);
    }
    if(!plci->appl)
    {
      if (plci->State == LISTENING)
      {
        plci->notifiedcall=0;
        a->listen_active--;
      }
      plci->State = INC_DIS_PENDING;
      if(c_ind_mask_empty (plci))
      {
        plci->State = IDLE;
        if (plci->NL.Id && !plci->nl_remove_id)
        {
          mixer_remove (plci);
          nl_req_ncci(plci,REMOVE,0);
        }
        if (!plci->sig_remove_id)
        {
          plci->internal_command = 0;
          sig_req(plci,REMOVE,0);
        }
        send_req(plci);
      }
    }
    else
    {
        /* collision of DISCONNECT or CONNECT_RES with HANGUP can   */
        /* result in a second HANGUP! Don't generate another        */
        /* DISCONNECT                                               */
      if(plci->State!=IDLE && plci->State!=INC_DIS_PENDING)
      {
        if(plci->State==RESUMING)
        {
          PUT_WORD(&resume_cau[4],i);
          sendf(plci->appl,_FACILITY_I,Id,0,"ws", (word)3, resume_cau);
        }
        plci->State = INC_DIS_PENDING;
        sendf(plci->appl,_DISCONNECT_I,Id,0,"w",i);
      }
    }
    break;

  case SSEXT_IND:
    SendSSExtInd(NULL,plci,Id,multi_ssext_parms);
    break;

  case VSWITCH_REQ:
    VSwitchReqInd(plci,Id,multi_vswitch_parms);
    break;
  case VSWITCH_IND:
 if(plci->relatedPTYPLCI &&
  plci->vswitchstate==3 &&
  plci->relatedPTYPLCI->vswitchstate==3 &&
  parms[MAXPARMSIDS-1][0])
 {
  add_p(plci->relatedPTYPLCI,SMSG,parms[MAXPARMSIDS-1]);
  sig_req(plci->relatedPTYPLCI,VSWITCH_REQ,0);
  send_req(plci->relatedPTYPLCI);
 }
    else VSwitchReqInd(plci,Id,multi_vswitch_parms);
    break;

  }
}


static void SendSetupInfo(APPL   * appl, PLCI   * plci, dword Id, byte   * * parms, byte Info_Sent_Flag)
{
  word i;
  byte   * ie;
  word Info_Number;
  byte   * Info_Element;
  word Info_Mask = 0;

  dbug(1,dprintf("SetupInfo"));

  for(i=0; i<MAXPARMSIDS; i++) {
    ie = parms[i];
    Info_Number = 0;
    Info_Element = ie;
    if(ie[0]) {
      switch(i) {
      case 0:
        dbug(1,dprintf("CPN "));
        Info_Number = 0x0070;
        Info_Mask   = 0x80;
        Info_Sent_Flag = true;
        break;
      case 8:  /* display      */
        dbug(1,dprintf("display(%d)",i));
        Info_Number = 0x0028;
        Info_Mask = 0x04;
        Info_Sent_Flag = true;
        break;
      case 16: /* Channel Id */
        dbug(1,dprintf("CHI"));
        Info_Number = 0x0018;
        Info_Mask = 0x100;
        Info_Sent_Flag = true;
        mixer_set_bchannel_id (plci, Info_Element);
        break;
      case 19: /* Redirected Number */
        dbug(1,dprintf("RDN"));
        Info_Number = 0x0074;
        Info_Mask = 0x400;
        Info_Sent_Flag = true;
        break;
      case 20: /* Redirected Number extended */
        dbug(1,dprintf("RDX"));
        Info_Number = 0x0073;
        Info_Mask = 0x400;
        Info_Sent_Flag = true;
        break;
      case 22: /* Redirecing Number  */
        dbug(1,dprintf("RIN"));
        Info_Number = 0x0076;
        Info_Mask = 0x400;
        Info_Sent_Flag = true;
        break;
      default:
        Info_Number = 0;
        break;
      }
    }

    if(i==MAXPARMSIDS-2){ /* to indicate the message type "Setup" */
      Info_Number = 0x8000 |5;
      Info_Mask = 0x10;
      Info_Element = "";
    }

    if(Info_Sent_Flag && Info_Number){
      if(plci->adapter->Info_Mask[appl->Id-1] & Info_Mask) {
        sendf(appl,_INFO_I,Id,0,"wS",Info_Number,Info_Element);
      }
    }
  }
}


static void SendInfo(PLCI *plci, dword Id, byte **parms, byte iesent)
{
  word i;
  word j;
  word k;
  byte   * ie;
  word Info_Number;
  byte   * Info_Element;
  word Info_Mask = 0;
  static byte charges[5] = {4,0,0,0,0};
  static byte cause[] = {0x02,0x80,0x00};
  APPL   *appl;

  dbug(1,dprintf("InfoParse "));

  if(
        !plci->appl
        && !plci->State
        && plci->Sig.Ind!=NCR_FACILITY
      )
  {
    dbug(1,dprintf("NoParse "));
    return;
  }
  cause[2] = 0;
  for(i=0; i<MAXPARMSIDS; i++) {
    ie = parms[i];
    Info_Number = 0;
    Info_Element = ie;
    if(ie[0]) {
      switch(i) {
      case 0:
        dbug(1,dprintf("CPN "));
        Info_Number = 0x0070;
        Info_Mask   = 0x80;
        break;
      case 7: /* ESC_CAU */
        dbug(1,dprintf("cau(0x%x)",ie[2]));
        Info_Number = 0x0008;
        Info_Mask = 0x00;
        cause[2] = ie[2];
        Info_Element = NULL;
        break;
      case 8:  /* display      */
        dbug(1,dprintf("display(%d)",i));
        Info_Number = 0x0028;
        Info_Mask = 0x04;
        break;
      case 9:  /* Date display */
        dbug(1,dprintf("date(%d)",i));
        Info_Number = 0x0029;
        Info_Mask = 0x02;
        break;
      case 10: /* charges */
        for(j=0;j<4;j++) charges[1+j] = 0;
        for(j=0; j<ie[0] && !(ie[1+j]&0x80); j++);
        for(k=1,j++; j<ie[0] && k<=4; j++,k++) charges[k] = ie[1+j];
        Info_Number = 0x4000;
        Info_Mask = 0x40;
        Info_Element = charges;
        break;
      case 11: /* user user info */
        dbug(1,dprintf("uui"));
        Info_Number = 0x007E;
        Info_Mask = 0x08;
        break;
      case 12: /* congestion receiver ready */
        dbug(1,dprintf("clRDY"));
        Info_Number = 0x00B0;
        Info_Mask = 0x08;
        Info_Element = "";
        break;
      case 13: /* congestion receiver not ready */
        dbug(1,dprintf("clNRDY"));
        Info_Number = 0x00BF;
        Info_Mask = 0x08;
        Info_Element = "";
        break;
      case 15: /* Keypad Facility */
        dbug(1,dprintf("KEY"));
        Info_Number = 0x002C;
        Info_Mask = 0x20;
        break;
      case 16: /* Channel Id */
        dbug(1,dprintf("CHI"));
        Info_Number = 0x0018;
        Info_Mask = 0x100;
        mixer_set_bchannel_id (plci, Info_Element);
        break;
      case 17: /* if no 1tr6 cause, send full cause, else esc_cause */
        dbug(1,dprintf("q9cau(0x%x)",ie[2]));
        if(!cause[2] || cause[2]<0x80) break;  /* eg. layer 1 error */
        Info_Number = 0x0008;
        Info_Mask = 0x01;
        if(cause[2] != ie[2]) Info_Element = cause;
        break;
      case 19: /* Redirected Number */
        dbug(1,dprintf("RDN"));
        Info_Number = 0x0074;
        Info_Mask = 0x400;
        break;
      case 22: /* Redirecing Number  */
        dbug(1,dprintf("RIN"));
        Info_Number = 0x0076;
        Info_Mask = 0x400;
        break;
      case 23: /* Notification Indicator  */
        dbug(1,dprintf("NI"));
        Info_Number = (word)NI;
        Info_Mask = 0x210;
        break;
      case 26: /* Call State  */
        dbug(1,dprintf("CST"));
        Info_Number = (word)CST;
        Info_Mask = 0x01; /* do with cause i.e. for now */
        break;
      case MAXPARMSIDS-2:  /* Escape Message Type, must be the last indication */
        dbug(1,dprintf("ESC/MT[0x%x]",ie[3]));
        Info_Number = 0x8000 |ie[3];
        if(iesent) Info_Mask = 0xffff;
        else  Info_Mask = 0x10;
        Info_Element = "";
        break;
      default:
        Info_Number  = 0;
        Info_Mask    = 0;
        Info_Element = "";
        break;
      }
    }

    if(plci->Sig.Ind==NCR_FACILITY)           /* check controller broadcast */
    {
      for(j=0; j<max_appl; j++)
      {
        appl = &application[j];
        if(Info_Number
        && appl->Id
        && plci->adapter->Info_Mask[appl->Id-1] &Info_Mask)
        {
          dbug(1,dprintf("NCR_Ind"));
          iesent=true;
          sendf(&application[j],_INFO_I,Id&0x0f,0,"wS",Info_Number,Info_Element);
        }
      }
    }
    else if(!plci->appl)
    { /* overlap receiving broadcast */
      if(Info_Number==CPN
      || Info_Number==KEY
      || Info_Number==NI
      || Info_Number==DSP
      || Info_Number==UUI )
      {
        for(j=0; j<max_appl; j++)
        {
          if(test_c_ind_mask_bit (plci, j))
          {
            dbug(1,dprintf("Ovl_Ind"));
            iesent=true;
            sendf(&application[j],_INFO_I,Id,0,"wS",Info_Number,Info_Element);
          }
        }
      }
    }               /* all other signalling states */
    else if(Info_Number
    && plci->adapter->Info_Mask[plci->appl->Id-1] &Info_Mask)
    {
      dbug(1,dprintf("Std_Ind"));
      iesent=true;
      sendf(plci->appl,_INFO_I,Id,0,"wS",Info_Number,Info_Element);
    }
  }
}


static byte SendMultiIE(PLCI *plci, dword Id, byte **parms, byte ie_type,
			dword info_mask, byte setupParse)
{
  word i;
  word j;
  byte   * ie;
  word Info_Number;
  byte   * Info_Element;
  APPL   *appl;
  word Info_Mask = 0;
  byte iesent=0;

  if(
      !plci->appl
      && !plci->State
      && plci->Sig.Ind!=NCR_FACILITY
      && !setupParse
      )
  {
    dbug(1,dprintf("NoM-IEParse "));
    return 0;
  }
  dbug(1,dprintf("M-IEParse "));

  for(i=0; i<MAX_MULTI_IE; i++)
  {
    ie = parms[i];
    Info_Number = 0;
    Info_Element = ie;
    if(ie[0])
    {
      dbug(1,dprintf("[Ind0x%x]:IE=0x%x",plci->Sig.Ind,ie_type));
      Info_Number = (word)ie_type;
      Info_Mask = (word)info_mask;
    }

    if(plci->Sig.Ind==NCR_FACILITY)           /* check controller broadcast */
    {
      for(j=0; j<max_appl; j++)
      {
        appl = &application[j];
        if(Info_Number
        && appl->Id
        && plci->adapter->Info_Mask[appl->Id-1] &Info_Mask)
        {
          iesent = true;
          dbug(1,dprintf("Mlt_NCR_Ind"));
          sendf(&application[j],_INFO_I,Id&0x0f,0,"wS",Info_Number,Info_Element);
        }
      }
    }
    else if(!plci->appl && Info_Number)
    {                                        /* overlap receiving broadcast */
      for(j=0; j<max_appl; j++)
      {
        if(test_c_ind_mask_bit (plci, j))
        {
          iesent = true;
          dbug(1,dprintf("Mlt_Ovl_Ind"));
          sendf(&application[j],_INFO_I,Id,0,"wS",Info_Number,Info_Element);
        }
      }
    }                                        /* all other signalling states */
    else if(Info_Number
    && plci->adapter->Info_Mask[plci->appl->Id-1] &Info_Mask)
    {
      iesent = true;
      dbug(1,dprintf("Mlt_Std_Ind"));
      sendf(plci->appl,_INFO_I,Id,0,"wS",Info_Number,Info_Element);
    }
  }
  return iesent;
}

static void SendSSExtInd(APPL   * appl, PLCI   * plci, dword Id, byte   * * parms)
{
  word i;
   /* Format of multi_ssext_parms[i][]:
   0 byte length
   1 byte SSEXTIE
   2 byte SSEXT_REQ/SSEXT_IND
   3 byte length
   4 word SSExtCommand
   6... Params
   */
  if(
   plci
   && plci->State
   && plci->Sig.Ind!=NCR_FACILITY
    )
 for(i=0;i<MAX_MULTI_IE;i++)
    {
      if(parms[i][0]<6) continue;
   if(parms[i][2]==SSEXT_REQ) continue;

   if(appl)
   {
    parms[i][0]=0; /* kill it */
    sendf(appl,_MANUFACTURER_I,
    Id,
    0,
    "dwS",
    _DI_MANU_ID,
    _DI_SSEXT_CTRL,
    &parms[i][3]);
   }
   else if(plci->appl)
   {
    parms[i][0]=0; /* kill it */
    sendf(plci->appl,_MANUFACTURER_I,
    Id,
    0,
    "dwS",
    _DI_MANU_ID,
    _DI_SSEXT_CTRL,
    &parms[i][3]);
   }
    }
};

static void nl_ind(PLCI *plci)
{
  byte ch;
  word ncci;
  dword Id;
  DIVA_CAPI_ADAPTER   * a;
  word NCCIcode;
  APPL   * APPLptr;
  word count;
  word Num;
  word i, ncpi_state;
  byte len, ncci_state;
  word msg;
  word info = 0;
  word fax_feature_bits;
  byte fax_send_edata_ack;
  static byte v120_header_buffer[2 + 3];
  static word fax_info[] = {
    0,                     /* T30_SUCCESS                        */
    _FAX_NO_CONNECTION,    /* T30_ERR_NO_DIS_RECEIVED            */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_TIMEOUT_NO_RESPONSE        */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_RESPONSE          */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_TOO_MANY_REPEATS           */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_UNEXPECTED_MESSAGE         */
    _FAX_REMOTE_ABORT,     /* T30_ERR_UNEXPECTED_DCN             */
    _FAX_LOCAL_ABORT,      /* T30_ERR_DTC_UNSUPPORTED            */
    _FAX_TRAINING_ERROR,   /* T30_ERR_ALL_RATES_FAILED           */
    _FAX_TRAINING_ERROR,   /* T30_ERR_TOO_MANY_TRAINS            */
    _FAX_PARAMETER_ERROR,  /* T30_ERR_RECEIVE_CORRUPTED          */
    _FAX_REMOTE_ABORT,     /* T30_ERR_UNEXPECTED_DISC            */
    _FAX_LOCAL_ABORT,      /* T30_ERR_APPLICATION_DISC           */
    _FAX_REMOTE_REJECT,    /* T30_ERR_INCOMPATIBLE_DIS           */
    _FAX_LOCAL_ABORT,      /* T30_ERR_INCOMPATIBLE_DCS           */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_TIMEOUT_NO_COMMAND         */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_COMMAND           */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_TIMEOUT_COMMAND_TOO_LONG   */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_TIMEOUT_RESPONSE_TOO_LONG  */
    _FAX_NO_CONNECTION,    /* T30_ERR_NOT_IDENTIFIED             */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_SUPERVISORY_TIMEOUT        */
    _FAX_PARAMETER_ERROR,  /* T30_ERR_TOO_LONG_SCAN_LINE         */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_PAGE_AFTER_MPS    */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_PAGE_AFTER_CFR    */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_DCS_AFTER_FTT     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_DCS_AFTER_EOM     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_DCS_AFTER_MPS     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_DCN_AFTER_MCF     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_DCN_AFTER_RTN     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_CFR               */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_MCF_AFTER_EOP     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_MCF_AFTER_EOM     */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_RETRY_NO_MCF_AFTER_MPS     */
    0x331d,                /* T30_ERR_SUB_SEP_UNSUPPORTED        */
    0x331e,                /* T30_ERR_PWD_UNSUPPORTED            */
    0x331f,                /* T30_ERR_SUB_SEP_PWD_UNSUPPORTED    */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_INVALID_COMMAND_FRAME      */
    _FAX_PARAMETER_ERROR,  /* T30_ERR_UNSUPPORTED_PAGE_CODING    */
    _FAX_PARAMETER_ERROR,  /* T30_ERR_INVALID_PAGE_CODING        */
    _FAX_REMOTE_REJECT,    /* T30_ERR_INCOMPATIBLE_PAGE_CONFIG   */
    _FAX_LOCAL_ABORT,      /* T30_ERR_TIMEOUT_FROM_APPLICATION   */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_V34FAX_NO_REACTION_ON_MARK */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_V34FAX_TRAINING_TIMEOUT    */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_V34FAX_UNEXPECTED_V21      */
    _FAX_PROTOCOL_ERROR,   /* T30_ERR_V34FAX_PRIMARY_CTS_ON      */
    _FAX_LOCAL_ABORT,      /* T30_ERR_V34FAX_TURNAROUND_POLLING  */
    _FAX_LOCAL_ABORT       /* T30_ERR_V34FAX_V8_INCOMPATIBILITY  */
  };

    byte dtmf_code_buffer[CAPIDTMF_RECV_DIGIT_BUFFER_SIZE + 1];


  static word rtp_info[] = {
    GOOD,                  /* RTP_SUCCESS                       */
    0x3600                 /* RTP_ERR_SSRC_OR_PAYLOAD_CHANGE    */
  };

  static dword udata_forwarding_table[0x100 / sizeof(dword)] =
  {
    0x0020301e, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
  };

  ch = plci->NL.IndCh;
  a = plci->adapter;
  ncci = a->ch_ncci[ch];
  Id = (((dword)(ncci ? ncci : ch)) << 16) | (((word) plci->Id) << 8) | a->Id;
  if(plci->tel) Id|=EXT_CONTROLLER;
  APPLptr = plci->appl;
  dbug(1,dprintf("NL_IND-Id(NL:0x%x)=0x%08lx,plci=%x,tel=%x,state=0x%x,ch=0x%x,chs=%d,Ind=%x",
    plci->NL.Id,Id,plci->Id,plci->tel,plci->State,ch,plci->channels,plci->NL.Ind &0x0f));

  /* in the case if no connect_active_Ind was sent to the appl we wait for */

  if (plci->nl_remove_id)
  {
    plci->NL.RNR = 2; /* discard */
    dbug(1,dprintf("NL discard while remove pending"));
    return;
  }
  if((plci->NL.Ind &0x0f)==N_CONNECT)
  {
    if(plci->State==INC_DIS_PENDING
    || plci->State==OUTG_DIS_PENDING
    || plci->State==IDLE)
    {
      plci->NL.RNR = 2; /* discard */
      dbug(1,dprintf("discard n_connect"));
      return;
    }
    if(plci->State < INC_ACT_PENDING)
    {
      plci->NL.RNR = 1; /* flow control */
      channel_x_off (plci, ch, N_XON_CONNECT_IND);
      return;
    }
  }

  if(!APPLptr)                         /* no application or invalid data */
  {                                    /* while reloading the DSP        */
    dbug(1,dprintf("discard1"));
    plci->NL.RNR = 2;
    return;
  }

  if (((plci->NL.Ind &0x0f) == N_UDATA)
     && (((plci->B2_prot != B2_SDLC) && ((plci->B1_resource == 17) || (plci->B1_resource == 18)))
        || (plci->B2_prot == 7)
        || (plci->B3_prot == 7)) )
  {
    plci->ncpi_buffer[0] = 0;

    ncpi_state = plci->ncpi_state;
    if (plci->NL.complete == 1)
    {
      byte  * data = &plci->NL.RBuffer->P[0];

      if ((plci->NL.RBuffer->length >= 12)
        &&( (*data == DSP_UDATA_INDICATION_DCD_ON)
          ||(*data == DSP_UDATA_INDICATION_CTS_ON)) )
      {
        word conn_opt, ncpi_opt = 0x00;
/*      HexDump ("MDM N_UDATA:", plci->NL.RBuffer->length, data); */

        if (*data == DSP_UDATA_INDICATION_DCD_ON)
          plci->ncpi_state |= NCPI_MDM_DCD_ON_RECEIVED;
        if (*data == DSP_UDATA_INDICATION_CTS_ON)
          plci->ncpi_state |= NCPI_MDM_CTS_ON_RECEIVED;

        data++;    /* indication code */
        data += 2; /* timestamp */
        if ((*data == DSP_CONNECTED_NORM_V18) || (*data == DSP_CONNECTED_NORM_VOWN))
          ncpi_state &= ~(NCPI_MDM_DCD_ON_RECEIVED | NCPI_MDM_CTS_ON_RECEIVED);
        data++;    /* connected norm */
        conn_opt = GET_WORD(data);
        data += 2; /* connected options */

        PUT_WORD (&(plci->ncpi_buffer[1]), (word)(GET_DWORD(data) & 0x0000FFFF));

        if (conn_opt & DSP_CONNECTED_OPTION_MASK_V42)
        {
          ncpi_opt |= MDM_NCPI_ECM_V42;
        }
        else if (conn_opt & DSP_CONNECTED_OPTION_MASK_MNP)
        {
          ncpi_opt |= MDM_NCPI_ECM_MNP;
        }
        else
        {
          ncpi_opt |= MDM_NCPI_TRANSPARENT;
        }
        if (conn_opt & DSP_CONNECTED_OPTION_MASK_COMPRESSION)
        {
          ncpi_opt |= MDM_NCPI_COMPRESSED;
        }
        PUT_WORD (&(plci->ncpi_buffer[3]), ncpi_opt);
        plci->ncpi_buffer[0] = 4;

        plci->ncpi_state |= NCPI_VALID_CONNECT_B3_IND | NCPI_VALID_CONNECT_B3_ACT | NCPI_VALID_DISC_B3_IND;
      }
    }
    if (plci->B3_prot == 7)
    {
      if (((a->ncci_state[ncci] == INC_ACT_PENDING) || (a->ncci_state[ncci] == OUTG_CON_PENDING))
       && (plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
       && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
      {
        a->ncci_state[ncci] = INC_ACT_PENDING;
        sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"S",plci->ncpi_buffer);
        plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
      }
    }

    if (!((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[plci->appl->Id-1])
        & ((1L << PRIVATE_V18) | (1L << PRIVATE_VOWN)))
     || !(ncpi_state & NCPI_MDM_DCD_ON_RECEIVED)
     || !(ncpi_state & NCPI_MDM_CTS_ON_RECEIVED))

    {
      plci->NL.RNR = 2;
      return;
    }
  }

  if(plci->NL.complete == 2)
    {
    if (((plci->NL.Ind &0x0f) == N_UDATA)
     && !(udata_forwarding_table[plci->RData[0].P[0] >> 5] & (1L << (plci->RData[0].P[0] & 0x1f))))
    {
      switch(plci->RData[0].P[0])
      {

      case DTMF_UDATA_INDICATION_FAX_CALLING_TONE:
        if (plci->dtmf_rec_active & DTMF_LISTEN_ACTIVE_FLAG)
          sendf(plci->appl, _FACILITY_I, Id & 0xffffL, 0,"ws", SELECTOR_DTMF, "\x01X");
        break;
      case DTMF_UDATA_INDICATION_ANSWER_TONE:
        if (plci->dtmf_rec_active & DTMF_LISTEN_ACTIVE_FLAG)
          sendf(plci->appl, _FACILITY_I, Id & 0xffffL, 0,"ws", SELECTOR_DTMF, "\x01Y");
        break;
      case DTMF_UDATA_INDICATION_DIGITS_RECEIVED:
        dtmf_indication (Id, plci, plci->RData[0].P, plci->RData[0].PLength);
        break;
      case DTMF_UDATA_INDICATION_DIGITS_SENT:
        dtmf_confirmation (Id, plci);
        break;


      case UDATA_INDICATION_MIXER_TAP_DATA:
        capidtmf_recv_process_block (&(plci->capidtmf_state), plci->RData[0].P + 1, (word)(plci->RData[0].PLength - 1));
 i = capidtmf_indication (&(plci->capidtmf_state), dtmf_code_buffer + 1);
 if (i != 0)
 {
   dtmf_code_buffer[0] = DTMF_UDATA_INDICATION_DIGITS_RECEIVED;
          dtmf_indication (Id, plci, dtmf_code_buffer, (word)(i + 1));
 }
        break;


      case UDATA_INDICATION_MIXER_COEFS_SET:
        mixer_indication_coefs_set (Id, plci);
        break;
      case UDATA_INDICATION_XCONNECT_FROM:
        mixer_indication_xconnect_from (Id, plci, plci->RData[0].P, plci->RData[0].PLength);
        break;
      case UDATA_INDICATION_XCONNECT_TO:
        mixer_indication_xconnect_to (Id, plci, plci->RData[0].P, plci->RData[0].PLength);
        break;


      case LEC_UDATA_INDICATION_DISABLE_DETECT:
        ec_indication (Id, plci, plci->RData[0].P, plci->RData[0].PLength);
        break;



      default:
        break;
      }
    }
    else
  {
      if ((plci->RData[0].PLength != 0)
     && ((plci->B2_prot == B2_V120_ASYNC)
      || (plci->B2_prot == B2_V120_ASYNC_V42BIS)
      || (plci->B2_prot == B2_V120_BIT_TRANSPARENT)))
    {

      sendf(plci->appl,_DATA_B3_I,Id,0,
            "dwww",
            plci->RData[1].P,
              (plci->NL.RNum < 2) ? 0 : plci->RData[1].PLength,
            plci->RNum,
            plci->RFlags);

    }
    else
    {

      sendf(plci->appl,_DATA_B3_I,Id,0,
            "dwww",
            plci->RData[0].P,
            plci->RData[0].PLength,
            plci->RNum,
            plci->RFlags);

    }
    }
    return;
  }

  fax_feature_bits = 0;
  if((plci->NL.Ind &0x0f)==N_CONNECT ||
     (plci->NL.Ind &0x0f)==N_CONNECT_ACK ||
     (plci->NL.Ind &0x0f)==N_DISC ||
     (plci->NL.Ind &0x0f)==N_EDATA ||
     (plci->NL.Ind &0x0f)==N_DISC_ACK)
  {
    info = 0;
    plci->ncpi_buffer[0] = 0;
    switch (plci->B3_prot) {
    case  0: /*XPARENT*/
    case  1: /*T.90 NL*/
      break;    /* no network control protocol info - jfr */
    case  2: /*ISO8202*/
    case  3: /*X25 DCE*/
      for(i=0; i<plci->NL.RLength; i++) plci->ncpi_buffer[4+i] = plci->NL.RBuffer->P[i];
      plci->ncpi_buffer[0] = (byte)(i+3);
      plci->ncpi_buffer[1] = (byte)(plci->NL.Ind &N_D_BIT? 1:0);
      plci->ncpi_buffer[2] = 0;
      plci->ncpi_buffer[3] = 0;
      break;
    case  4: /*T.30 - FAX*/
    case  5: /*T.30 - FAX*/
      if(plci->NL.RLength>=sizeof(T30_INFO))
      {
        dbug(1,dprintf("FaxStatus %04x", ((T30_INFO   *)plci->NL.RBuffer->P)->code));
        len = 9;
        PUT_WORD(&(plci->ncpi_buffer[1]),((T30_INFO   *)plci->NL.RBuffer->P)->rate_div_2400 * 2400);
        fax_feature_bits = GET_WORD(&((T30_INFO   *)plci->NL.RBuffer->P)->feature_bits_low);
        i = (((T30_INFO   *)plci->NL.RBuffer->P)->resolution & T30_RESOLUTION_R8_0770_OR_200) ? 0x0001 : 0x0000;
        if (plci->B3_prot == 5)
        {
          if (!(fax_feature_bits & T30_FEATURE_BIT_ECM))
            i |= 0x8000; /* This is not an ECM connection */
          if (fax_feature_bits & T30_FEATURE_BIT_T6_CODING)
            i |= 0x4000; /* This is a connection with MMR compression */
          if (fax_feature_bits & T30_FEATURE_BIT_2D_CODING)
            i |= 0x2000; /* This is a connection with MR compression */
          if (fax_feature_bits & T30_FEATURE_BIT_MORE_DOCUMENTS)
            i |= 0x0004; /* More documents */
          if (fax_feature_bits & T30_FEATURE_BIT_POLLING)
            i |= 0x0002; /* Fax-polling indication */
        }
        dbug(1,dprintf("FAX Options %04x %04x",fax_feature_bits,i));
        PUT_WORD(&(plci->ncpi_buffer[3]),i);
        PUT_WORD(&(plci->ncpi_buffer[5]),((T30_INFO   *)plci->NL.RBuffer->P)->data_format);
        plci->ncpi_buffer[7] = ((T30_INFO   *)plci->NL.RBuffer->P)->pages_low;
        plci->ncpi_buffer[8] = ((T30_INFO   *)plci->NL.RBuffer->P)->pages_high;
        plci->ncpi_buffer[len] = 0;
        if(((T30_INFO   *)plci->NL.RBuffer->P)->station_id_len)
        {
          plci->ncpi_buffer[len] = 20;
          for (i = 0; i < 20; i++)
            plci->ncpi_buffer[++len] = ((T30_INFO   *)plci->NL.RBuffer->P)->station_id[i];
        }
        if (((plci->NL.Ind & 0x0f) == N_DISC) || ((plci->NL.Ind & 0x0f) == N_DISC_ACK))
        {
	  if (((T30_INFO   *)plci->NL.RBuffer->P)->code < ARRAY_SIZE(fax_info))
            info = fax_info[((T30_INFO   *)plci->NL.RBuffer->P)->code];
          else
            info = _FAX_PROTOCOL_ERROR;
        }

        if ((plci->requested_options_conn | plci->requested_options | a->requested_options_table[plci->appl->Id-1])
          & ((1L << PRIVATE_FAX_SUB_SEP_PWD) | (1L << PRIVATE_FAX_NONSTANDARD)))
        {
          i = ((word)(((T30_INFO *) 0)->station_id + 20)) + ((T30_INFO   *)plci->NL.RBuffer->P)->head_line_len;
          while (i < plci->NL.RBuffer->length)
            plci->ncpi_buffer[++len] = plci->NL.RBuffer->P[i++];
        }

        plci->ncpi_buffer[0] = len;
        fax_feature_bits = GET_WORD(&((T30_INFO   *)plci->NL.RBuffer->P)->feature_bits_low);
        PUT_WORD(&((T30_INFO   *)plci->fax_connect_info_buffer)->feature_bits_low, fax_feature_bits);

        plci->ncpi_state |= NCPI_VALID_CONNECT_B3_IND;
 if (((plci->NL.Ind &0x0f) == N_CONNECT_ACK)
         || (((plci->NL.Ind &0x0f) == N_CONNECT)
          && (fax_feature_bits & T30_FEATURE_BIT_POLLING))
         || (((plci->NL.Ind &0x0f) == N_EDATA)
          && ((((T30_INFO   *)plci->NL.RBuffer->P)->code == EDATA_T30_TRAIN_OK)
           || (((T30_INFO   *)plci->NL.RBuffer->P)->code == EDATA_T30_DIS)
           || (((T30_INFO   *)plci->NL.RBuffer->P)->code == EDATA_T30_DTC))))
 {
          plci->ncpi_state |= NCPI_VALID_CONNECT_B3_ACT;
 }
 if (((plci->NL.Ind &0x0f) == N_DISC)
  || ((plci->NL.Ind &0x0f) == N_DISC_ACK)
  || (((plci->NL.Ind &0x0f) == N_EDATA)
   && (((T30_INFO   *)plci->NL.RBuffer->P)->code == EDATA_T30_EOP_CAPI)))
 {
          plci->ncpi_state |= NCPI_VALID_CONNECT_B3_ACT | NCPI_VALID_DISC_B3_IND;
 }
      }
      break;

    case B3_RTP:
      if (((plci->NL.Ind & 0x0f) == N_DISC) || ((plci->NL.Ind & 0x0f) == N_DISC_ACK))
      {
        if (plci->NL.RLength != 0)
        {
          info = rtp_info[plci->NL.RBuffer->P[0]];
          plci->ncpi_buffer[0] = plci->NL.RLength - 1;
          for (i = 1; i < plci->NL.RLength; i++)
            plci->ncpi_buffer[i] = plci->NL.RBuffer->P[i];
        }
      }
      break;

    }
    plci->NL.RNR = 2;
  }
  switch(plci->NL.Ind &0x0f) {
  case N_EDATA:
    if ((plci->B3_prot == 4) || (plci->B3_prot == 5))
    {
      dbug(1,dprintf("EDATA ncci=0x%x state=%d code=%02x", ncci, a->ncci_state[ncci],
        ((T30_INFO   *)plci->NL.RBuffer->P)->code));
      fax_send_edata_ack = (((T30_INFO   *)(plci->fax_connect_info_buffer))->operating_mode == T30_OPERATING_MODE_CAPI_NEG);

      if ((plci->nsf_control_bits & T30_NSF_CONTROL_BIT_ENABLE_NSF)
       && (plci->nsf_control_bits & (T30_NSF_CONTROL_BIT_NEGOTIATE_IND | T30_NSF_CONTROL_BIT_NEGOTIATE_RESP))
       && (((T30_INFO   *)plci->NL.RBuffer->P)->code == EDATA_T30_DIS)
       && (a->ncci_state[ncci] == OUTG_CON_PENDING)
       && (plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
       && !(plci->ncpi_state & NCPI_NEGOTIATE_B3_SENT))
      {
        ((T30_INFO   *)(plci->fax_connect_info_buffer))->code = ((T30_INFO   *)plci->NL.RBuffer->P)->code;
        sendf(plci->appl,_MANUFACTURER_I,Id,0,"dwbS",_DI_MANU_ID,_DI_NEGOTIATE_B3,
          (byte)(plci->ncpi_buffer[0] + 1), plci->ncpi_buffer);
        plci->ncpi_state |= NCPI_NEGOTIATE_B3_SENT;
 if (plci->nsf_control_bits & T30_NSF_CONTROL_BIT_NEGOTIATE_RESP)
   fax_send_edata_ack = false;
      }

      if (a->manufacturer_features & MANUFACTURER_FEATURE_FAX_PAPER_FORMATS)
      {
        switch (((T30_INFO   *)plci->NL.RBuffer->P)->code)
        {
        case EDATA_T30_DIS:
          if ((a->ncci_state[ncci] == OUTG_CON_PENDING)
           && !(GET_WORD(&((T30_INFO   *)plci->fax_connect_info_buffer)->control_bits_low) & T30_CONTROL_BIT_REQUEST_POLLING)
           && (plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
           && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
          {
            a->ncci_state[ncci] = INC_ACT_PENDING;
            if (plci->B3_prot == 4)
              sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
            else
              sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"S",plci->ncpi_buffer);
            plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
          }
          break;

        case EDATA_T30_TRAIN_OK:
          if ((a->ncci_state[ncci] == INC_ACT_PENDING)
           && (plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
           && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
          {
            if (plci->B3_prot == 4)
              sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
            else
              sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"S",plci->ncpi_buffer);
            plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
          }
          break;

        case EDATA_T30_EOP_CAPI:
          if (a->ncci_state[ncci] == CONNECTED)
          {
            sendf(plci->appl,_DISCONNECT_B3_I,Id,0,"wS",GOOD,plci->ncpi_buffer);
            a->ncci_state[ncci] = INC_DIS_PENDING;
            plci->ncpi_state = 0;
     fax_send_edata_ack = false;
          }
          break;
        }
      }
      else
      {
        switch (((T30_INFO   *)plci->NL.RBuffer->P)->code)
        {
        case EDATA_T30_TRAIN_OK:
          if ((a->ncci_state[ncci] == INC_ACT_PENDING)
           && (plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
           && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
          {
            if (plci->B3_prot == 4)
              sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
            else
              sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"S",plci->ncpi_buffer);
            plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
          }
          break;
        }
      }
      if (fax_send_edata_ack)
      {
        ((T30_INFO   *)(plci->fax_connect_info_buffer))->code = ((T30_INFO   *)plci->NL.RBuffer->P)->code;
 plci->fax_edata_ack_length = 1;
        start_internal_command (Id, plci, fax_edata_ack_command);
      }
    }
    else
    {
      dbug(1,dprintf("EDATA ncci=0x%x state=%d", ncci, a->ncci_state[ncci]));
    }
    break;
  case N_CONNECT:
    if (!a->ch_ncci[ch])
    {
      ncci = get_ncci (plci, ch, 0);
      Id = (Id & 0xffff) | (((dword) ncci) << 16);
    }
    dbug(1,dprintf("N_CONNECT: ch=%d state=%d plci=%lx plci_Id=%lx plci_State=%d",
      ch, a->ncci_state[ncci], a->ncci_plci[ncci], plci->Id, plci->State));

    msg = _CONNECT_B3_I;
    if (a->ncci_state[ncci] == IDLE)
      plci->channels++;
    else if (plci->B3_prot == 1)
      msg = _CONNECT_B3_T90_ACTIVE_I;

    a->ncci_state[ncci] = INC_CON_PENDING;
    if(plci->B3_prot == 4)
      sendf(plci->appl,msg,Id,0,"s","");
    else
      sendf(plci->appl,msg,Id,0,"S",plci->ncpi_buffer);
    break;
  case N_CONNECT_ACK:
    dbug(1,dprintf("N_connect_Ack"));
    if (plci->internal_command_queue[0]
     && ((plci->adjust_b_state == ADJUST_B_CONNECT_2)
      || (plci->adjust_b_state == ADJUST_B_CONNECT_3)
      || (plci->adjust_b_state == ADJUST_B_CONNECT_4)))
    {
      (*(plci->internal_command_queue[0]))(Id, plci, 0);
      if (!plci->internal_command)
        next_internal_command (Id, plci);
      break;
    }
    msg = _CONNECT_B3_ACTIVE_I;
    if (plci->B3_prot == 1)
    {
      if (a->ncci_state[ncci] != OUTG_CON_PENDING)
        msg = _CONNECT_B3_T90_ACTIVE_I;
      a->ncci_state[ncci] = INC_ACT_PENDING;
      sendf(plci->appl,msg,Id,0,"S",plci->ncpi_buffer);
    }
    else if ((plci->B3_prot == 4) || (plci->B3_prot == 5) || (plci->B3_prot == 7))
    {
      if ((a->ncci_state[ncci] == OUTG_CON_PENDING)
       && (plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
       && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
      {
        a->ncci_state[ncci] = INC_ACT_PENDING;
        if (plci->B3_prot == 4)
          sendf(plci->appl,msg,Id,0,"s","");
        else
          sendf(plci->appl,msg,Id,0,"S",plci->ncpi_buffer);
        plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
      }
    }
    else
    {
      a->ncci_state[ncci] = INC_ACT_PENDING;
      sendf(plci->appl,msg,Id,0,"S",plci->ncpi_buffer);
    }
    if (plci->adjust_b_restore)
    {
      plci->adjust_b_restore = false;
      start_internal_command (Id, plci, adjust_b_restore);
    }
    break;
  case N_DISC:
  case N_DISC_ACK:
    if (plci->internal_command_queue[0]
     && ((plci->internal_command == FAX_DISCONNECT_COMMAND_1)
      || (plci->internal_command == FAX_DISCONNECT_COMMAND_2)
      || (plci->internal_command == FAX_DISCONNECT_COMMAND_3)))
    {
      (*(plci->internal_command_queue[0]))(Id, plci, 0);
      if (!plci->internal_command)
        next_internal_command (Id, plci);
    }
    ncci_state = a->ncci_state[ncci];
    ncci_remove (plci, ncci, false);

        /* with N_DISC or N_DISC_ACK the IDI frees the respective   */
        /* channel, so we cannot store the state in ncci_state! The */
        /* information which channel we received a N_DISC is thus   */
        /* stored in the inc_dis_ncci_table buffer.                 */
    for(i=0; plci->inc_dis_ncci_table[i]; i++);
    plci->inc_dis_ncci_table[i] = (byte) ncci;

      /* need a connect_b3_ind before a disconnect_b3_ind with FAX */
    if (!plci->channels
     && (plci->B1_resource == 16)
     && (plci->State <= CONNECTED))
    {
      len = 9;
      i = ((T30_INFO   *)plci->fax_connect_info_buffer)->rate_div_2400 * 2400;
      PUT_WORD (&plci->ncpi_buffer[1], i);
      PUT_WORD (&plci->ncpi_buffer[3], 0);
      i = ((T30_INFO   *)plci->fax_connect_info_buffer)->data_format;
      PUT_WORD (&plci->ncpi_buffer[5], i);
      PUT_WORD (&plci->ncpi_buffer[7], 0);
      plci->ncpi_buffer[len] = 0;
      plci->ncpi_buffer[0] = len;
      if(plci->B3_prot == 4)
        sendf(plci->appl,_CONNECT_B3_I,Id,0,"s","");
      else
      {

        if ((plci->requested_options_conn | plci->requested_options | a->requested_options_table[plci->appl->Id-1])
          & ((1L << PRIVATE_FAX_SUB_SEP_PWD) | (1L << PRIVATE_FAX_NONSTANDARD)))
        {
          plci->ncpi_buffer[++len] = 0;
          plci->ncpi_buffer[++len] = 0;
          plci->ncpi_buffer[++len] = 0;
          plci->ncpi_buffer[0] = len;
        }

        sendf(plci->appl,_CONNECT_B3_I,Id,0,"S",plci->ncpi_buffer);
      }
      sendf(plci->appl,_DISCONNECT_B3_I,Id,0,"wS",info,plci->ncpi_buffer);
      plci->ncpi_state = 0;
      sig_req(plci,HANGUP,0);
      send_req(plci);
      plci->State = OUTG_DIS_PENDING;
      /* disc here */
    }
    else if ((a->manufacturer_features & MANUFACTURER_FEATURE_FAX_PAPER_FORMATS)
     && ((plci->B3_prot == 4) || (plci->B3_prot == 5))
     && ((ncci_state == INC_DIS_PENDING) || (ncci_state == IDLE)))
    {
      if (ncci_state == IDLE)
      {
        if (plci->channels)
          plci->channels--;
        if((plci->State==IDLE || plci->State==SUSPENDING) && !plci->channels){
          if(plci->State == SUSPENDING){
            sendf(plci->appl,
                  _FACILITY_I,
                  Id & 0xffffL,
                  0,
                  "ws", (word)3, "\x03\x04\x00\x00");
            sendf(plci->appl, _DISCONNECT_I, Id & 0xffffL, 0, "w", 0);
          }
          plci_remove(plci);
          plci->State=IDLE;
        }
      }
    }
    else if (plci->channels)
    {
      sendf(plci->appl,_DISCONNECT_B3_I,Id,0,"wS",info,plci->ncpi_buffer);
      plci->ncpi_state = 0;
      if ((ncci_state == OUTG_REJ_PENDING)
       && ((plci->B3_prot != B3_T90NL) && (plci->B3_prot != B3_ISO8208) && (plci->B3_prot != B3_X25_DCE)))
      {
        sig_req(plci,HANGUP,0);
        send_req(plci);
        plci->State = OUTG_DIS_PENDING;
      }
    }
    break;
  case N_RESET:
    a->ncci_state[ncci] = INC_RES_PENDING;
    sendf(plci->appl,_RESET_B3_I,Id,0,"S",plci->ncpi_buffer);
    break;
  case N_RESET_ACK:
    a->ncci_state[ncci] = CONNECTED;
    sendf(plci->appl,_RESET_B3_I,Id,0,"S",plci->ncpi_buffer);
    break;

  case N_UDATA:
    if (!(udata_forwarding_table[plci->NL.RBuffer->P[0] >> 5] & (1L << (plci->NL.RBuffer->P[0] & 0x1f))))
    {
      plci->RData[0].P = plci->internal_ind_buffer + (-((int)(long)(plci->internal_ind_buffer)) & 3);
      plci->RData[0].PLength = INTERNAL_IND_BUFFER_SIZE;
      plci->NL.R = plci->RData;
      plci->NL.RNum = 1;
      return;
    }
  case N_BDATA:
  case N_DATA:
    if (((a->ncci_state[ncci] != CONNECTED) && (plci->B2_prot == 1)) /* transparent */
     || (a->ncci_state[ncci] == IDLE)
     || (a->ncci_state[ncci] == INC_DIS_PENDING))
    {
      plci->NL.RNR = 2;
      break;
    }
    if ((a->ncci_state[ncci] != CONNECTED)
     && (a->ncci_state[ncci] != OUTG_DIS_PENDING)
     && (a->ncci_state[ncci] != OUTG_REJ_PENDING))
    {
      dbug(1,dprintf("flow control"));
      plci->NL.RNR = 1; /* flow control  */
      channel_x_off (plci, ch, 0);
      break;
    }

    NCCIcode = ncci | (((word)a->Id) << 8);

                /* count all buffers within the Application pool    */
                /* belonging to the same NCCI. If this is below the */
                /* number of buffers available per NCCI we accept   */
                /* this packet, otherwise we reject it              */
    count = 0;
    Num = 0xffff;
    for(i=0; i<APPLptr->MaxBuffer; i++) {
      if(NCCIcode==APPLptr->DataNCCI[i]) count++;
      if(!APPLptr->DataNCCI[i] && Num==0xffff) Num = i;
    }

    if(count>=APPLptr->MaxNCCIData || Num==0xffff)
    {
      dbug(3,dprintf("Flow-Control"));
      plci->NL.RNR = 1;
      if( ++(APPLptr->NCCIDataFlowCtrlTimer)>=
       (word)((a->manufacturer_features & MANUFACTURER_FEATURE_OOB_CHANNEL) ? 40 : 2000))
      {
        plci->NL.RNR = 2;
        dbug(3,dprintf("DiscardData"));
      } else {
        channel_x_off (plci, ch, 0);
      }
      break;
    }
    else
    {
      APPLptr->NCCIDataFlowCtrlTimer = 0;
    }

    plci->RData[0].P = ReceiveBufferGet(APPLptr,Num);
    if(!plci->RData[0].P) {
      plci->NL.RNR = 1;
      channel_x_off (plci, ch, 0);
      break;
    }

    APPLptr->DataNCCI[Num] = NCCIcode;
    APPLptr->DataFlags[Num] = (plci->Id<<8) | (plci->NL.Ind>>4);
    dbug(3,dprintf("Buffer(%d), Max = %d",Num,APPLptr->MaxBuffer));

    plci->RNum = Num;
    plci->RFlags = plci->NL.Ind>>4;
    plci->RData[0].PLength = APPLptr->MaxDataLength;
    plci->NL.R = plci->RData;
    if ((plci->NL.RLength != 0)
     && ((plci->B2_prot == B2_V120_ASYNC)
      || (plci->B2_prot == B2_V120_ASYNC_V42BIS)
      || (plci->B2_prot == B2_V120_BIT_TRANSPARENT)))
    {
      plci->RData[1].P = plci->RData[0].P;
      plci->RData[1].PLength = plci->RData[0].PLength;
      plci->RData[0].P = v120_header_buffer + (-((int) v120_header_buffer) & 3);
      if ((plci->NL.RBuffer->P[0] & V120_HEADER_EXTEND_BIT) || (plci->NL.RLength == 1))
        plci->RData[0].PLength = 1;
      else
        plci->RData[0].PLength = 2;
      if (plci->NL.RBuffer->P[0] & V120_HEADER_BREAK_BIT)
        plci->RFlags |= 0x0010;
      if (plci->NL.RBuffer->P[0] & (V120_HEADER_C1_BIT | V120_HEADER_C2_BIT))
        plci->RFlags |= 0x8000;
      plci->NL.RNum = 2;
    }
    else
    {
      if((plci->NL.Ind &0x0f)==N_UDATA)
        plci->RFlags |= 0x0010;

      else if ((plci->B3_prot == B3_RTP) && ((plci->NL.Ind & 0x0f) == N_BDATA))
        plci->RFlags |= 0x0001;

      plci->NL.RNum = 1;
    }
    break;
  case N_DATA_ACK:
    data_ack (plci, ch);
    break;
  default:
    plci->NL.RNR = 2;
    break;
  }
}

/*------------------------------------------------------------------*/
/* find a free PLCI                                                 */
/*------------------------------------------------------------------*/

static word get_plci(DIVA_CAPI_ADAPTER *a)
{
  word i,j;
  PLCI   * plci;

  dump_plcis (a);
  for(i=0;i<a->max_plci && a->plci[i].Id;i++);
  if(i==a->max_plci) {
    dbug(1,dprintf("get_plci: out of PLCIs"));
    return 0;
  }
  plci = &a->plci[i];
  plci->Id = (byte)(i+1);

  plci->Sig.Id = 0;
  plci->NL.Id = 0;
  plci->sig_req = 0;
  plci->nl_req = 0;

  plci->appl = NULL;
  plci->relatedPTYPLCI = NULL;
  plci->State = IDLE;
  plci->SuppState = IDLE;
  plci->channels = 0;
  plci->tel = 0;
  plci->B1_resource = 0;
  plci->B2_prot = 0;
  plci->B3_prot = 0;

  plci->command = 0;
  plci->m_command = 0;
  init_internal_command_queue (plci);
  plci->number = 0;
  plci->req_in_start = 0;
  plci->req_in = 0;
  plci->req_out = 0;
  plci->msg_in_write_pos = MSG_IN_QUEUE_SIZE;
  plci->msg_in_read_pos = MSG_IN_QUEUE_SIZE;
  plci->msg_in_wrap_pos = MSG_IN_QUEUE_SIZE;

  plci->data_sent = false;
  plci->send_disc = 0;
  plci->sig_global_req = 0;
  plci->sig_remove_id = 0;
  plci->nl_global_req = 0;
  plci->nl_remove_id = 0;
  plci->adv_nl = 0;
  plci->manufacturer = false;
  plci->call_dir = CALL_DIR_OUT | CALL_DIR_ORIGINATE;
  plci->spoofed_msg = 0;
  plci->ptyState = 0;
  plci->cr_enquiry = false;
  plci->hangup_flow_ctrl_timer = 0;

  plci->ncci_ring_list = 0;
  for(j=0;j<MAX_CHANNELS_PER_PLCI;j++) plci->inc_dis_ncci_table[j] = 0;
  clear_c_ind_mask (plci);
  set_group_ind_mask (plci);
  plci->fax_connect_info_length = 0;
  plci->nsf_control_bits = 0;
  plci->ncpi_state = 0x00;
  plci->ncpi_buffer[0] = 0;

  plci->requested_options_conn = 0;
  plci->requested_options = 0;
  plci->notifiedcall = 0;
  plci->vswitchstate = 0;
  plci->vsprot = 0;
  plci->vsprotdialect = 0;
  init_b1_config (plci);
  dbug(1,dprintf("get_plci(%x)",plci->Id));
  return i+1;
}

/*------------------------------------------------------------------*/
/* put a parameter in the parameter buffer                          */
/*------------------------------------------------------------------*/

static void add_p(PLCI   * plci, byte code, byte   * p)
{
  word p_length;

  p_length = 0;
  if(p) p_length = p[0];
  add_ie(plci, code, p, p_length);
}

/*------------------------------------------------------------------*/
/* put a structure in the parameter buffer                          */
/*------------------------------------------------------------------*/
static void add_s(PLCI   * plci, byte code, API_PARSE * p)
{
  if(p) add_ie(plci, code, p->info, (word)p->length);
}

/*------------------------------------------------------------------*/
/* put multiple structures in the parameter buffer                  */
/*------------------------------------------------------------------*/
static void add_ss(PLCI   * plci, byte code, API_PARSE * p)
{
  byte i;

  if(p){
    dbug(1,dprintf("add_ss(%x,len=%d)",code,p->length));
    for(i=2;i<(byte)p->length;i+=p->info[i]+2){
      dbug(1,dprintf("add_ss_ie(%x,len=%d)",p->info[i-1],p->info[i]));
      add_ie(plci, p->info[i-1], (byte   *)&(p->info[i]), (word)p->info[i]);
    }
  }
}

/*------------------------------------------------------------------*/
/* return the channel number sent by the application in a esc_chi   */
/*------------------------------------------------------------------*/
static byte getChannel(API_PARSE * p)
{
  byte i;

  if(p){
    for(i=2;i<(byte)p->length;i+=p->info[i]+2){
      if(p->info[i]==2){
        if(p->info[i-1]==ESC && p->info[i+1]==CHI) return (p->info[i+2]);
      }
    }
  }
  return 0;
}


/*------------------------------------------------------------------*/
/* put an information element in the parameter buffer               */
/*------------------------------------------------------------------*/

static void add_ie(PLCI   * plci, byte code, byte   * p, word p_length)
{
  word i;

  if(!(code &0x80) && !p_length) return;

  if(plci->req_in==plci->req_in_start) {
    plci->req_in +=2;
  }
  else {
    plci->req_in--;
  }
  plci->RBuffer[plci->req_in++] = code;

  if(p) {
    plci->RBuffer[plci->req_in++] = (byte)p_length;
    for(i=0;i<p_length;i++) plci->RBuffer[plci->req_in++] = p[1+i];
  }

  plci->RBuffer[plci->req_in++] = 0;
}

/*------------------------------------------------------------------*/
/* put a unstructured data into the buffer                          */
/*------------------------------------------------------------------*/

static void add_d(PLCI *plci, word length, byte *p)
{
  word i;

  if(plci->req_in==plci->req_in_start) {
    plci->req_in +=2;
  }
  else {
    plci->req_in--;
  }
  for(i=0;i<length;i++) plci->RBuffer[plci->req_in++] = p[i];
}

/*------------------------------------------------------------------*/
/* put parameters from the Additional Info parameter in the         */
/* parameter buffer                                                 */
/*------------------------------------------------------------------*/

static void add_ai(PLCI *plci, API_PARSE *ai)
{
  word i;
    API_PARSE ai_parms[5];

  for(i=0;i<5;i++) ai_parms[i].length = 0;

  if(!ai->length)
    return;
  if(api_parse(&ai->info[1], (word)ai->length, "ssss", ai_parms))
    return;

  add_s (plci,KEY,&ai_parms[1]);
  add_s (plci,UUI,&ai_parms[2]);
  add_ss(plci,FTY,&ai_parms[3]);
}

/*------------------------------------------------------------------*/
/* put parameter for b1 protocol in the parameter buffer            */
/*------------------------------------------------------------------*/

static word add_b1(PLCI *plci, API_PARSE *bp, word b_channel_info,
		   word b1_facilities)
{
    API_PARSE bp_parms[8];
    API_PARSE mdm_cfg[9];
    API_PARSE global_config[2];
    byte cai[256];
  byte resource[] = {5,9,13,12,16,39,9,17,17,18};
  byte voice_cai[] = "\x06\x14\x00\x00\x00\x00\x08";
  word i;

    API_PARSE mdm_cfg_v18[4];
  word j, n, w;
  dword d;


  for(i=0;i<8;i++) bp_parms[i].length = 0;
  for(i=0;i<2;i++) global_config[i].length = 0;

  dbug(1,dprintf("add_b1"));
  api_save_msg(bp, "s", &plci->B_protocol);

  if(b_channel_info==2){
    plci->B1_resource = 0;
    adjust_b1_facilities (plci, plci->B1_resource, b1_facilities);
    add_p(plci, CAI, "\x01\x00");
    dbug(1,dprintf("Cai=1,0 (no resource)"));
    return 0;
  }

  if(plci->tel == CODEC_PERMANENT) return 0;
  else if(plci->tel == CODEC){
    plci->B1_resource = 1;
    adjust_b1_facilities (plci, plci->B1_resource, b1_facilities);
    add_p(plci, CAI, "\x01\x01");
    dbug(1,dprintf("Cai=1,1 (Codec)"));
    return 0;
  }
  else if(plci->tel == ADV_VOICE){
    plci->B1_resource = add_b1_facilities (plci, 9, (word)(b1_facilities | B1_FACILITY_VOICE));
    adjust_b1_facilities (plci, plci->B1_resource, (word)(b1_facilities | B1_FACILITY_VOICE));
    voice_cai[1] = plci->B1_resource;
    PUT_WORD (&voice_cai[5], plci->appl->MaxDataLength);
    add_p(plci, CAI, voice_cai);
    dbug(1,dprintf("Cai=1,0x%x (AdvVoice)",voice_cai[1]));
    return 0;
  }
  plci->call_dir &= ~(CALL_DIR_ORIGINATE | CALL_DIR_ANSWER);
  if (plci->call_dir & CALL_DIR_OUT)
    plci->call_dir |= CALL_DIR_ORIGINATE;
  else if (plci->call_dir & CALL_DIR_IN)
    plci->call_dir |= CALL_DIR_ANSWER;

  if(!bp->length){
    plci->B1_resource = 0x5;
    adjust_b1_facilities (plci, plci->B1_resource, b1_facilities);
    add_p(plci, CAI, "\x01\x05");
    return 0;
  }

  dbug(1,dprintf("b_prot_len=%d",(word)bp->length));
  if(bp->length>256) return _WRONG_MESSAGE_FORMAT;
  if(api_parse(&bp->info[1], (word)bp->length, "wwwsssb", bp_parms))
  {
    bp_parms[6].length = 0;
    if(api_parse(&bp->info[1], (word)bp->length, "wwwsss", bp_parms))
    {
      dbug(1,dprintf("b-form.!"));
      return _WRONG_MESSAGE_FORMAT;
    }
  }
  else if (api_parse(&bp->info[1], (word)bp->length, "wwwssss", bp_parms))
  {
    dbug(1,dprintf("b-form.!"));
    return _WRONG_MESSAGE_FORMAT;
  }

  if(bp_parms[6].length)
  {
    if(api_parse(&bp_parms[6].info[1], (word)bp_parms[6].length, "w", global_config))
    {
      return _WRONG_MESSAGE_FORMAT;
    }
    switch(GET_WORD(global_config[0].info))
    {
    case 1:
      plci->call_dir = (plci->call_dir & ~CALL_DIR_ANSWER) | CALL_DIR_ORIGINATE;
      break;
    case 2:
      plci->call_dir = (plci->call_dir & ~CALL_DIR_ORIGINATE) | CALL_DIR_ANSWER;
      break;
    }
  }
  dbug(1,dprintf("call_dir=%04x", plci->call_dir));


  if ((GET_WORD(bp_parms[0].info) == B1_RTP)
   && (plci->adapter->man_profile.private_options & (1L << PRIVATE_RTP)))
  {
    plci->B1_resource = add_b1_facilities (plci, 31, (word)(b1_facilities & ~B1_FACILITY_VOICE));
    adjust_b1_facilities (plci, plci->B1_resource, (word)(b1_facilities & ~B1_FACILITY_VOICE));
    cai[1] = plci->B1_resource;
    cai[2] = 0;
    cai[3] = 0;
    cai[4] = 0;
    PUT_WORD(&cai[5],plci->appl->MaxDataLength);
    for (i = 0; i < bp_parms[3].length; i++)
      cai[7+i] = bp_parms[3].info[1+i];
    cai[0] = 6 + bp_parms[3].length;
    add_p(plci, CAI, cai);
    return 0;
  }


  if ((GET_WORD(bp_parms[0].info) == B1_PIAFS)
   && (plci->adapter->man_profile.private_options & (1L << PRIVATE_PIAFS)))
  {
    plci->B1_resource = add_b1_facilities (plci, 35/* PIAFS HARDWARE FACILITY */, (word)(b1_facilities & ~B1_FACILITY_VOICE));
    adjust_b1_facilities (plci, plci->B1_resource, (word)(b1_facilities & ~B1_FACILITY_VOICE));
    cai[1] = plci->B1_resource;
    cai[2] = 0;
    cai[3] = 0;
    cai[4] = 0;
    PUT_WORD(&cai[5],plci->appl->MaxDataLength);
    cai[0] = 6;
    add_p(plci, CAI, cai);
    return 0;
  }


  if ((GET_WORD(bp_parms[0].info) >= 32)
   || (!((1L << GET_WORD(bp_parms[0].info)) & plci->adapter->profile.B1_Protocols)
    && ((GET_WORD(bp_parms[0].info) != 3)
     || !((1L << B1_HDLC) & plci->adapter->profile.B1_Protocols)
     || ((bp_parms[3].length != 0) && (GET_WORD(&bp_parms[3].info[1]) != 0) && (GET_WORD(&bp_parms[3].info[1]) != 56000)))))
  {
    return _B1_NOT_SUPPORTED;
  }
  plci->B1_resource = add_b1_facilities (plci, resource[GET_WORD(bp_parms[0].info)],
    (word)(b1_facilities & ~B1_FACILITY_VOICE));
  adjust_b1_facilities (plci, plci->B1_resource, (word)(b1_facilities & ~B1_FACILITY_VOICE));
  cai[0] = 6;
  cai[1] = plci->B1_resource;
  for (i=2;i<sizeof(cai);i++) cai[i] = 0;

  if ((GET_WORD(bp_parms[0].info) == B1_MODEM_ALL_NEGOTIATE)
   || (GET_WORD(bp_parms[0].info) == B1_MODEM_ASYNC)
   || (GET_WORD(bp_parms[0].info) == B1_MODEM_SYNC_HDLC))
  { /* B1 - modem */
    for (i=0;i<7;i++) mdm_cfg[i].length = 0;

    if (bp_parms[3].length)
    {
      if(api_parse(&bp_parms[3].info[1],(word)bp_parms[3].length,"wwwwww", mdm_cfg))
      {
        return (_WRONG_MESSAGE_FORMAT);
      }
        
      cai[2] = 0; /* Bit rate for adaptation */

      dbug(1,dprintf("MDM Max Bit Rate:<%d>", GET_WORD(mdm_cfg[0].info)));

      PUT_WORD (&cai[13], 0);                          /* Min Tx speed */
      PUT_WORD (&cai[15], GET_WORD(mdm_cfg[0].info)); /* Max Tx speed */
      PUT_WORD (&cai[17], 0);                          /* Min Rx speed */
      PUT_WORD (&cai[19], GET_WORD(mdm_cfg[0].info)); /* Max Rx speed */

      cai[3] = 0; /* Async framing parameters */
      switch (GET_WORD (mdm_cfg[2].info))
      {       /* Parity     */
      case 1: /* odd parity */
        cai[3] |= (DSP_CAI_ASYNC_PARITY_ENABLE | DSP_CAI_ASYNC_PARITY_ODD);
        dbug(1,dprintf("MDM: odd parity"));
        break;

      case 2: /* even parity */
        cai[3] |= (DSP_CAI_ASYNC_PARITY_ENABLE | DSP_CAI_ASYNC_PARITY_EVEN);
        dbug(1,dprintf("MDM: even parity"));
        break;

      default:
        dbug(1,dprintf("MDM: no parity"));
        break;
      }

      switch (GET_WORD (mdm_cfg[3].info))
      {       /* stop bits   */
      case 1: /* 2 stop bits */
        cai[3] |= DSP_CAI_ASYNC_TWO_STOP_BITS;
        dbug(1,dprintf("MDM: 2 stop bits"));
        break;

      default:
        dbug(1,dprintf("MDM: 1 stop bit"));
        break;
      }

      switch (GET_WORD (mdm_cfg[1].info))
      {     /* char length */
      case 5:
        cai[3] |= DSP_CAI_ASYNC_CHAR_LENGTH_5;
        dbug(1,dprintf("MDM: 5 bits"));
        break;

      case 6:
        cai[3] |= DSP_CAI_ASYNC_CHAR_LENGTH_6;
        dbug(1,dprintf("MDM: 6 bits"));
        break;

      case 7:
        cai[3] |= DSP_CAI_ASYNC_CHAR_LENGTH_7;
        dbug(1,dprintf("MDM: 7 bits"));
        break;

      default:
        dbug(1,dprintf("MDM: 8 bits"));
        break;
      }

      cai[7] = 0; /* Line taking options */
      cai[8] = 0; /* Modulation negotiation options */
      cai[9] = 0; /* Modulation options */

      if (((plci->call_dir & CALL_DIR_ORIGINATE) != 0) ^ ((plci->call_dir & CALL_DIR_OUT) != 0))
      {
        cai[9] |= DSP_CAI_MODEM_REVERSE_DIRECTION;
        dbug(1, dprintf("MDM: Reverse direction"));
      }

      if (GET_WORD (mdm_cfg[4].info) & MDM_CAPI_DISABLE_RETRAIN)
      {
        cai[9] |= DSP_CAI_MODEM_DISABLE_RETRAIN;
        dbug(1, dprintf("MDM: Disable retrain"));
      }

      if (GET_WORD (mdm_cfg[4].info) & MDM_CAPI_DISABLE_RING_TONE)
      {
        cai[7] |= DSP_CAI_MODEM_DISABLE_CALLING_TONE | DSP_CAI_MODEM_DISABLE_ANSWER_TONE;
        dbug(1, dprintf("MDM: Disable ring tone"));
      }

      if (GET_WORD (mdm_cfg[4].info) & MDM_CAPI_GUARD_1800)
      {
        cai[8] |= DSP_CAI_MODEM_GUARD_TONE_1800HZ;
        dbug(1, dprintf("MDM: 1800 guard tone"));
      }
      else if (GET_WORD (mdm_cfg[4].info) & MDM_CAPI_GUARD_550 )
      {
        cai[8] |= DSP_CAI_MODEM_GUARD_TONE_550HZ;
        dbug(1, dprintf("MDM: 550 guard tone"));
      }

      if ((GET_WORD (mdm_cfg[5].info) & 0x00ff) == MDM_CAPI_NEG_V100)
      {
        cai[8] |= DSP_CAI_MODEM_NEGOTIATE_V100;
        dbug(1, dprintf("MDM: V100"));
      }
      else if ((GET_WORD (mdm_cfg[5].info) & 0x00ff) == MDM_CAPI_NEG_MOD_CLASS)
      {
        cai[8] |= DSP_CAI_MODEM_NEGOTIATE_IN_CLASS;
        dbug(1, dprintf("MDM: IN CLASS"));
      }
      else if ((GET_WORD (mdm_cfg[5].info) & 0x00ff) == MDM_CAPI_NEG_DISABLED)
      {
        cai[8] |= DSP_CAI_MODEM_NEGOTIATE_DISABLED;
        dbug(1, dprintf("MDM: DISABLED"));
      }
      cai[0] = 20;

      if ((plci->adapter->man_profile.private_options & (1L << PRIVATE_V18))
       && (GET_WORD(mdm_cfg[5].info) & 0x8000)) /* Private V.18 enable */
      {
        plci->requested_options |= 1L << PRIVATE_V18;
      }
      if (GET_WORD(mdm_cfg[5].info) & 0x4000) /* Private VOWN enable */
        plci->requested_options |= 1L << PRIVATE_VOWN;

      if ((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[plci->appl->Id-1])
        & ((1L << PRIVATE_V18) | (1L << PRIVATE_VOWN)))
      {
        if (!api_parse(&bp_parms[3].info[1],(word)bp_parms[3].length,"wwwwwws", mdm_cfg))
        {
          i = 27;
          if (mdm_cfg[6].length >= 4)
          {
            d = GET_DWORD(&mdm_cfg[6].info[1]);
            cai[7] |= (byte) d;          /* line taking options */
            cai[9] |= (byte)(d >> 8);    /* modulation options */
            cai[++i] = (byte)(d >> 16);  /* vown modulation options */
            cai[++i] = (byte)(d >> 24);
            if (mdm_cfg[6].length >= 8)
            {
              d = GET_DWORD(&mdm_cfg[6].info[5]);
              cai[10] |= (byte) d;        /* disabled modulations mask */
              cai[11] |= (byte)(d >> 8);
              if (mdm_cfg[6].length >= 12)
              {
                d = GET_DWORD(&mdm_cfg[6].info[9]);
                cai[12] = (byte) d;          /* enabled modulations mask */
                cai[++i] = (byte)(d >> 8);   /* vown enabled modulations */
                cai[++i] = (byte)(d >> 16);
                cai[++i] = (byte)(d >> 24);
                cai[++i] = 0;
                if (mdm_cfg[6].length >= 14)
                {
                  w = GET_WORD(&mdm_cfg[6].info[13]);
                  if (w != 0)
                    PUT_WORD(&cai[13], w);  /* min tx speed */
                  if (mdm_cfg[6].length >= 16)
                  {
                    w = GET_WORD(&mdm_cfg[6].info[15]);
                    if (w != 0)
                      PUT_WORD(&cai[15], w);  /* max tx speed */
                    if (mdm_cfg[6].length >= 18)
                    {
                      w = GET_WORD(&mdm_cfg[6].info[17]);
                      if (w != 0)
                        PUT_WORD(&cai[17], w);  /* min rx speed */
                      if (mdm_cfg[6].length >= 20)
                      {
                        w = GET_WORD(&mdm_cfg[6].info[19]);
                        if (w != 0)
                          PUT_WORD(&cai[19], w);  /* max rx speed */
                        if (mdm_cfg[6].length >= 22)
                        {
                          w = GET_WORD(&mdm_cfg[6].info[21]);
                          cai[23] = (byte)(-((short) w));  /* transmit level */
                          if (mdm_cfg[6].length >= 24)
                          {
                            w = GET_WORD(&mdm_cfg[6].info[23]);
                            cai[22] |= (byte) w;        /* info options mask */
                            cai[21] |= (byte)(w >> 8);  /* disabled symbol rates */
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          cai[27] = i - 27;
          i++;
          if (!api_parse(&bp_parms[3].info[1],(word)bp_parms[3].length,"wwwwwwss", mdm_cfg))
          {
            if (!api_parse(&mdm_cfg[7].info[1],(word)mdm_cfg[7].length,"sss", mdm_cfg_v18))
            {
              for (n = 0; n < 3; n++)
              {
                cai[i] = (byte)(mdm_cfg_v18[n].length);
                for (j = 1; j < ((word)(cai[i] + 1)); j++)
                  cai[i+j] = mdm_cfg_v18[n].info[j];
                i += cai[i] + 1;
              }
            }
          }
          cai[0] = (byte)(i - 1);
        }
      }

    }
  }
  if(GET_WORD(bp_parms[0].info)==2 ||                         /* V.110 async */
     GET_WORD(bp_parms[0].info)==3 )                          /* V.110 sync */
  {
    if(bp_parms[3].length){
      dbug(1,dprintf("V.110,%d",GET_WORD(&bp_parms[3].info[1])));
      switch(GET_WORD(&bp_parms[3].info[1])){                 /* Rate */
        case 0:
        case 56000:
          if(GET_WORD(bp_parms[0].info)==3){                  /* V.110 sync 56k */
            dbug(1,dprintf("56k sync HSCX"));
            cai[1] = 8;
            cai[2] = 0;
            cai[3] = 0;
          }
          else if(GET_WORD(bp_parms[0].info)==2){
            dbug(1,dprintf("56k async DSP"));
            cai[2] = 9;
          }
          break;
        case 50:     cai[2] = 1;  break;
        case 75:     cai[2] = 1;  break;
        case 110:    cai[2] = 1;  break;
        case 150:    cai[2] = 1;  break;
        case 200:    cai[2] = 1;  break;
        case 300:    cai[2] = 1;  break;
        case 600:    cai[2] = 1;  break;
        case 1200:   cai[2] = 2;  break;
        case 2400:   cai[2] = 3;  break;
        case 4800:   cai[2] = 4;  break;
        case 7200:   cai[2] = 10; break;
        case 9600:   cai[2] = 5;  break;
        case 12000:  cai[2] = 13; break;
        case 24000:  cai[2] = 0;  break;
        case 14400:  cai[2] = 11; break;
        case 19200:  cai[2] = 6;  break;
        case 28800:  cai[2] = 12; break;
        case 38400:  cai[2] = 7;  break;
        case 48000:  cai[2] = 8;  break;
        case 76:     cai[2] = 15; break;  /* 75/1200     */
        case 1201:   cai[2] = 14; break;  /* 1200/75     */
        case 56001:  cai[2] = 9;  break;  /* V.110 56000 */

        default:
          return _B1_PARM_NOT_SUPPORTED;
      }
      cai[3] = 0;
      if (cai[1] == 13)                                        /* v.110 async */
      {
        if (bp_parms[3].length >= 8)
        {
          switch (GET_WORD (&bp_parms[3].info[3]))
          {       /* char length */
          case 5:
            cai[3] |= DSP_CAI_ASYNC_CHAR_LENGTH_5;
            break;
          case 6:
            cai[3] |= DSP_CAI_ASYNC_CHAR_LENGTH_6;
            break;
          case 7:
            cai[3] |= DSP_CAI_ASYNC_CHAR_LENGTH_7;
            break;
          }
          switch (GET_WORD (&bp_parms[3].info[5]))
          {       /* Parity     */
          case 1: /* odd parity */
            cai[3] |= (DSP_CAI_ASYNC_PARITY_ENABLE | DSP_CAI_ASYNC_PARITY_ODD);
            break;
          case 2: /* even parity */
            cai[3] |= (DSP_CAI_ASYNC_PARITY_ENABLE | DSP_CAI_ASYNC_PARITY_EVEN);
            break;
          }
          switch (GET_WORD (&bp_parms[3].info[7]))
          {       /* stop bits   */
          case 1: /* 2 stop bits */
            cai[3] |= DSP_CAI_ASYNC_TWO_STOP_BITS;
            break;
          }
        }
      }
    }
    else if(cai[1]==8 || GET_WORD(bp_parms[0].info)==3 ){
      dbug(1,dprintf("V.110 default 56k sync"));
      cai[1] = 8;
      cai[2] = 0;
      cai[3] = 0;
    }
    else {
      dbug(1,dprintf("V.110 default 9600 async"));
      cai[2] = 5;
    }
  }
  PUT_WORD(&cai[5],plci->appl->MaxDataLength);
  dbug(1,dprintf("CAI[%d]=%x,%x,%x,%x,%x,%x", cai[0], cai[1], cai[2], cai[3], cai[4], cai[5], cai[6]));
/* HexDump ("CAI", sizeof(cai), &cai[0]); */

  add_p(plci, CAI, cai);
  return 0;
}

/*------------------------------------------------------------------*/
/* put parameter for b2 and B3  protocol in the parameter buffer    */
/*------------------------------------------------------------------*/

static word add_b23(PLCI *plci, API_PARSE *bp)
{
  word i, fax_control_bits;
  byte pos, len;
  byte SAPI = 0x40;  /* default SAPI 16 for x.31 */
    API_PARSE bp_parms[8];
  API_PARSE * b1_config;
  API_PARSE * b2_config;
    API_PARSE b2_config_parms[8];
  API_PARSE * b3_config;
    API_PARSE b3_config_parms[6];
    API_PARSE global_config[2];

  static byte llc[3] = {2,0,0};
  static byte dlc[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  static byte nlc[256];
  static byte lli[12] = {1,1};

  const byte llc2_out[] = {1,2,4,6,2,0,0,0, X75_V42BIS,V120_L2,V120_V42BIS,V120_L2,6};
  const byte llc2_in[]  = {1,3,4,6,3,0,0,0, X75_V42BIS,V120_L2,V120_V42BIS,V120_L2,6};

  const byte llc3[] = {4,3,2,2,6,6,0};
  const byte header[] = {0,2,3,3,0,0,0};

  for(i=0;i<8;i++) bp_parms[i].length = 0;
  for(i=0;i<6;i++) b2_config_parms[i].length = 0;
  for(i=0;i<5;i++) b3_config_parms[i].length = 0;

  lli[0] = 1;
  lli[1] = 1;
  if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_XONOFF_FLOW_CONTROL)
    lli[1] |= 2;
  if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_OOB_CHANNEL)
    lli[1] |= 4;

  if ((lli[1] & 0x02) && (diva_xdi_extended_features & DIVA_CAPI_USE_CMA)) {
    lli[1] |= 0x10;
    if (plci->rx_dma_descriptor <= 0) {
      plci->rx_dma_descriptor=diva_get_dma_descriptor(plci,&plci->rx_dma_magic);
      if (plci->rx_dma_descriptor >= 0)
        plci->rx_dma_descriptor++;
    }
    if (plci->rx_dma_descriptor > 0) {
      lli[0] = 6;
      lli[1] |= 0x40;
      lli[2] = (byte)(plci->rx_dma_descriptor - 1);
      lli[3] = (byte)plci->rx_dma_magic;
      lli[4] = (byte)(plci->rx_dma_magic >>  8);
      lli[5] = (byte)(plci->rx_dma_magic >> 16);
      lli[6] = (byte)(plci->rx_dma_magic >> 24);
    }
  }

  if (DIVA_CAPI_SUPPORTS_NO_CANCEL(plci->adapter)) {
    lli[1] |= 0x20;
  }

  dbug(1,dprintf("add_b23"));
  api_save_msg(bp, "s", &plci->B_protocol);

  if(!bp->length && plci->tel)
  {
    plci->adv_nl = true;
    dbug(1,dprintf("Default adv.Nl"));
    add_p(plci,LLI,lli);
    plci->B2_prot = 1 /*XPARENT*/;
    plci->B3_prot = 0 /*XPARENT*/;
    llc[1] = 2;
    llc[2] = 4;
    add_p(plci, LLC, llc);
    dlc[0] = 2;
    PUT_WORD(&dlc[1],plci->appl->MaxDataLength);
    add_p(plci, DLC, dlc);
    return 0;
  }

  if(!bp->length) /*default*/
  {   
    dbug(1,dprintf("ret default"));
    add_p(plci,LLI,lli);
    plci->B2_prot = 0 /*X.75   */;
    plci->B3_prot = 0 /*XPARENT*/;
    llc[1] = 1;
    llc[2] = 4;
    add_p(plci, LLC, llc);
    dlc[0] = 2;
    PUT_WORD(&dlc[1],plci->appl->MaxDataLength);
    add_p(plci, DLC, dlc);
    return 0;
  }
  dbug(1,dprintf("b_prot_len=%d",(word)bp->length));
  if((word)bp->length > 256)    return _WRONG_MESSAGE_FORMAT;

  if(api_parse(&bp->info[1], (word)bp->length, "wwwsssb", bp_parms))
  {
    bp_parms[6].length = 0;
    if(api_parse(&bp->info[1], (word)bp->length, "wwwsss", bp_parms))
    {
      dbug(1,dprintf("b-form.!"));
      return _WRONG_MESSAGE_FORMAT;
    }
  }
  else if (api_parse(&bp->info[1], (word)bp->length, "wwwssss", bp_parms))
  {
    dbug(1,dprintf("b-form.!"));
    return _WRONG_MESSAGE_FORMAT;
  }

  if(plci->tel==ADV_VOICE) /* transparent B on advanced voice */
  {  
    if(GET_WORD(bp_parms[1].info)!=1
    || GET_WORD(bp_parms[2].info)!=0) return _B2_NOT_SUPPORTED;
    plci->adv_nl = true;
  }
  else if(plci->tel) return _B2_NOT_SUPPORTED;


  if ((GET_WORD(bp_parms[1].info) == B2_RTP)
   && (GET_WORD(bp_parms[2].info) == B3_RTP)
   && (plci->adapter->man_profile.private_options & (1L << PRIVATE_RTP)))
  {
    add_p(plci,LLI,lli);
    plci->B2_prot = (byte) GET_WORD(bp_parms[1].info);
    plci->B3_prot = (byte) GET_WORD(bp_parms[2].info);
    llc[1] = (plci->call_dir & (CALL_DIR_ORIGINATE | CALL_DIR_FORCE_OUTG_NL)) ? 14 : 13;
    llc[2] = 4;
    add_p(plci, LLC, llc);
    dlc[0] = 2;
    PUT_WORD(&dlc[1],plci->appl->MaxDataLength);
    dlc[3] = 3; /* Addr A */
    dlc[4] = 1; /* Addr B */
    dlc[5] = 7; /* modulo mode */
    dlc[6] = 7; /* window size */
    dlc[7] = 0; /* XID len Lo  */
    dlc[8] = 0; /* XID len Hi  */
    for (i = 0; i < bp_parms[4].length; i++)
      dlc[9+i] = bp_parms[4].info[1+i];
    dlc[0] = (byte)(8 + bp_parms[4].length);
    add_p(plci, DLC, dlc);
    for (i = 0; i < bp_parms[5].length; i++)
      nlc[1+i] = bp_parms[5].info[1+i];
    nlc[0] = (byte)(bp_parms[5].length);
    add_p(plci, NLC, nlc);
    return 0;
  }



  if ((GET_WORD(bp_parms[1].info) >= 32)
   || (!((1L << GET_WORD(bp_parms[1].info)) & plci->adapter->profile.B2_Protocols)
    && ((GET_WORD(bp_parms[1].info) != B2_PIAFS)
     || !(plci->adapter->man_profile.private_options & (1L << PRIVATE_PIAFS)))))

  {
    return _B2_NOT_SUPPORTED;
  }
  if ((GET_WORD(bp_parms[2].info) >= 32)
   || !((1L << GET_WORD(bp_parms[2].info)) & plci->adapter->profile.B3_Protocols))
  {
    return _B3_NOT_SUPPORTED;
  }
  if ((GET_WORD(bp_parms[1].info) != B2_SDLC)
   && ((GET_WORD(bp_parms[0].info) == B1_MODEM_ALL_NEGOTIATE)
    || (GET_WORD(bp_parms[0].info) == B1_MODEM_ASYNC)
    || (GET_WORD(bp_parms[0].info) == B1_MODEM_SYNC_HDLC)))
  {
    return (add_modem_b23 (plci, bp_parms));
  }

  add_p(plci,LLI,lli);

  plci->B2_prot = (byte) GET_WORD(bp_parms[1].info);
  plci->B3_prot = (byte) GET_WORD(bp_parms[2].info);
  if(plci->B2_prot==12) SAPI = 0; /* default SAPI D-channel */

  if(bp_parms[6].length)
  {
    if(api_parse(&bp_parms[6].info[1], (word)bp_parms[6].length, "w", global_config))
    {
      return _WRONG_MESSAGE_FORMAT;
    }
    switch(GET_WORD(global_config[0].info))
    {
    case 1:
      plci->call_dir = (plci->call_dir & ~CALL_DIR_ANSWER) | CALL_DIR_ORIGINATE;
      break;
    case 2:
      plci->call_dir = (plci->call_dir & ~CALL_DIR_ORIGINATE) | CALL_DIR_ANSWER;
      break;
    }
  }
  dbug(1,dprintf("call_dir=%04x", plci->call_dir));


  if (plci->B2_prot == B2_PIAFS)
    llc[1] = PIAFS_CRC;
  else
/* IMPLEMENT_PIAFS */
  {
    llc[1] = (plci->call_dir & (CALL_DIR_ORIGINATE | CALL_DIR_FORCE_OUTG_NL)) ?
             llc2_out[GET_WORD(bp_parms[1].info)] : llc2_in[GET_WORD(bp_parms[1].info)];
  }
  llc[2] = llc3[GET_WORD(bp_parms[2].info)];

  add_p(plci, LLC, llc);

  dlc[0] = 2;
  PUT_WORD(&dlc[1], plci->appl->MaxDataLength +
                      header[GET_WORD(bp_parms[2].info)]);

  b1_config = &bp_parms[3];
  nlc[0] = 0;
  if(plci->B3_prot == 4
  || plci->B3_prot == 5)
  {
    for (i=0;i<sizeof(T30_INFO);i++) nlc[i] = 0;
    nlc[0] = sizeof(T30_INFO);
    if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_FAX_PAPER_FORMATS)
      ((T30_INFO *)&nlc[1])->operating_mode = T30_OPERATING_MODE_CAPI;
    ((T30_INFO *)&nlc[1])->rate_div_2400 = 0xff;
    if(b1_config->length>=2)
    {
      ((T30_INFO *)&nlc[1])->rate_div_2400 = (byte)(GET_WORD(&b1_config->info[1])/2400);
    }
  }
  b2_config = &bp_parms[4];


  if (llc[1] == PIAFS_CRC)
  {
    if (plci->B3_prot != B3_TRANSPARENT)
    {
      return _B_STACK_NOT_SUPPORTED;
    }
    if(b2_config->length && api_parse(&b2_config->info[1], (word)b2_config->length, "bwww", b2_config_parms)) {
      return _WRONG_MESSAGE_FORMAT;
    }
    PUT_WORD(&dlc[1],plci->appl->MaxDataLength);
    dlc[3] = 0; /* Addr A */
    dlc[4] = 0; /* Addr B */
    dlc[5] = 0; /* modulo mode */
    dlc[6] = 0; /* window size */
    if (b2_config->length >= 7){
      dlc[ 7] = 7; 
      dlc[ 8] = 0; 
      dlc[ 9] = b2_config_parms[0].info[0]; /* PIAFS protocol Speed configuration */
      dlc[10] = b2_config_parms[1].info[0]; /* V.42bis P0 */
      dlc[11] = b2_config_parms[1].info[1]; /* V.42bis P0 */
      dlc[12] = b2_config_parms[2].info[0]; /* V.42bis P1 */
      dlc[13] = b2_config_parms[2].info[1]; /* V.42bis P1 */
      dlc[14] = b2_config_parms[3].info[0]; /* V.42bis P2 */
      dlc[15] = b2_config_parms[3].info[1]; /* V.42bis P2 */
      dlc[ 0] = 15;
      if(b2_config->length >= 8) { /* PIAFS control abilities */
        dlc[ 7] = 10; 
        dlc[16] = 2; /* Length of PIAFS extention */
        dlc[17] = PIAFS_UDATA_ABILITIES; /* control (UDATA) ability */
        dlc[18] = b2_config_parms[4].info[0]; /* value */
        dlc[ 0] = 18;
      }
    }
    else /* default values, 64K, variable, no compression */
    {
      dlc[ 7] = 7; 
      dlc[ 8] = 0; 
      dlc[ 9] = 0x03; /* PIAFS protocol Speed configuration */
      dlc[10] = 0x03; /* V.42bis P0 */
      dlc[11] = 0;    /* V.42bis P0 */
      dlc[12] = 0;    /* V.42bis P1 */
      dlc[13] = 0;    /* V.42bis P1 */
      dlc[14] = 0;    /* V.42bis P2 */
      dlc[15] = 0;    /* V.42bis P2 */
    dlc[ 0] = 15;
    }
    add_p(plci, DLC, dlc);
  }
  else

  if ((llc[1] == V120_L2) || (llc[1] == V120_V42BIS))
  {
    if (plci->B3_prot != B3_TRANSPARENT)
      return _B_STACK_NOT_SUPPORTED;

    dlc[0] = 6;
    PUT_WORD (&dlc[1], GET_WORD (&dlc[1]) + 2);
    dlc[3] = 0x08;
    dlc[4] = 0x01;
    dlc[5] = 127;
    dlc[6] = 7;
    if (b2_config->length != 0)
    {
      if((llc[1]==V120_V42BIS) && api_parse(&b2_config->info[1], (word)b2_config->length, "bbbbwww", b2_config_parms)) {
        return _WRONG_MESSAGE_FORMAT;
      }
      dlc[3] = (byte)((b2_config->info[2] << 3) | ((b2_config->info[1] >> 5) & 0x04));
      dlc[4] = (byte)((b2_config->info[1] << 1) | 0x01);
      if (b2_config->info[3] != 128)
      {
        dbug(1,dprintf("1D-dlc= %x %x %x %x %x", dlc[0], dlc[1], dlc[2], dlc[3], dlc[4]));
        return _B2_PARM_NOT_SUPPORTED;
      }
      dlc[5] = (byte)(b2_config->info[3] - 1);
      dlc[6] = b2_config->info[4];
      if(llc[1]==V120_V42BIS){
        if (b2_config->length >= 10){
          dlc[ 7] = 6; 
          dlc[ 8] = 0; 
          dlc[ 9] = b2_config_parms[4].info[0];
          dlc[10] = b2_config_parms[4].info[1];
          dlc[11] = b2_config_parms[5].info[0];
          dlc[12] = b2_config_parms[5].info[1];
          dlc[13] = b2_config_parms[6].info[0];
          dlc[14] = b2_config_parms[6].info[1];
          dlc[ 0] = 14;
          dbug(1,dprintf("b2_config_parms[4].info[0] [1]:  %x %x", b2_config_parms[4].info[0], b2_config_parms[4].info[1]));
          dbug(1,dprintf("b2_config_parms[5].info[0] [1]:  %x %x", b2_config_parms[5].info[0], b2_config_parms[5].info[1]));
          dbug(1,dprintf("b2_config_parms[6].info[0] [1]:  %x %x", b2_config_parms[6].info[0], b2_config_parms[6].info[1]));
        }
        else {
          dlc[ 6] = 14;
        }
      }
    }
  }
  else
  {
    if(b2_config->length)
    {
      dbug(1,dprintf("B2-Config"));
      if(llc[1]==X75_V42BIS){
        if(api_parse(&b2_config->info[1], (word)b2_config->length, "bbbbwww", b2_config_parms))
        {
          return _WRONG_MESSAGE_FORMAT;
        }
      }
      else {
        if(api_parse(&b2_config->info[1], (word)b2_config->length, "bbbbs", b2_config_parms))
        {
          return _WRONG_MESSAGE_FORMAT;
        }
      }
          /* if B2 Protocol is LAPD, b2_config structure is different */
      if(llc[1]==6)
      {
        dlc[0] = 4;
        if(b2_config->length>=1) dlc[2] = b2_config->info[1];      /* TEI */
        else dlc[2] = 0x01;
        if( (b2_config->length>=2) && (plci->B2_prot==12) )
        {
          SAPI = b2_config->info[2];    /* SAPI */
        }
        dlc[1] = SAPI;
        if( (b2_config->length>=3) && (b2_config->info[3]==128) )
        {
          dlc[3] = 127;      /* Mode */
        }
        else
        {
          dlc[3] = 7;        /* Mode */
        }
   
        if(b2_config->length>=4) dlc[4] = b2_config->info[4];      /* Window */
        else dlc[4] = 1;
        dbug(1,dprintf("D-dlc[%d]=%x,%x,%x,%x", dlc[0], dlc[1], dlc[2], dlc[3], dlc[4]));
        if(b2_config->length>5) return _B2_PARM_NOT_SUPPORTED;
      }
      else
      {
        dlc[0] = (byte)(b2_config_parms[4].length+6);
        dlc[3] = b2_config->info[1];
        dlc[4] = b2_config->info[2];
        if(b2_config->info[3]!=8 && b2_config->info[3]!=128){
          dbug(1,dprintf("1D-dlc= %x %x %x %x %x", dlc[0], dlc[1], dlc[2], dlc[3], dlc[4]));
          return _B2_PARM_NOT_SUPPORTED;
        }

        dlc[5] = (byte)(b2_config->info[3]-1);
        dlc[6] = b2_config->info[4];
        if(dlc[6]>dlc[5]){
          dbug(1,dprintf("2D-dlc= %x %x %x %x %x %x %x", dlc[0], dlc[1], dlc[2], dlc[3], dlc[4], dlc[5], dlc[6]));
          return _B2_PARM_NOT_SUPPORTED;
        }
 
        if(llc[1]==X75_V42BIS) {
          if (b2_config->length >= 10){
            dlc[ 7] = 6; 
            dlc[ 8] = 0; 
            dlc[ 9] = b2_config_parms[4].info[0];
            dlc[10] = b2_config_parms[4].info[1];
            dlc[11] = b2_config_parms[5].info[0];
            dlc[12] = b2_config_parms[5].info[1];
            dlc[13] = b2_config_parms[6].info[0];
            dlc[14] = b2_config_parms[6].info[1];
            dlc[ 0] = 14;
            dbug(1,dprintf("b2_config_parms[4].info[0] [1]:  %x %x", b2_config_parms[4].info[0], b2_config_parms[4].info[1]));
            dbug(1,dprintf("b2_config_parms[5].info[0] [1]:  %x %x", b2_config_parms[5].info[0], b2_config_parms[5].info[1]));
            dbug(1,dprintf("b2_config_parms[6].info[0] [1]:  %x %x", b2_config_parms[6].info[0], b2_config_parms[6].info[1]));
          }
          else {
            dlc[ 6] = 14;
          }

        }
        else {
          PUT_WORD(&dlc[7], (word)b2_config_parms[4].length);
          for(i=0; i<b2_config_parms[4].length; i++)
            dlc[11+i] = b2_config_parms[4].info[1+i];
        }
      }
    }
  }
  add_p(plci, DLC, dlc);

  b3_config = &bp_parms[5];
  if(b3_config->length)
  {
    if(plci->B3_prot == 4 
    || plci->B3_prot == 5)
    {
      if(api_parse(&b3_config->info[1], (word)b3_config->length, "wwss", b3_config_parms))
      {
        return _WRONG_MESSAGE_FORMAT;
      }
      i = GET_WORD((byte   *)(b3_config_parms[0].info));
      ((T30_INFO *)&nlc[1])->resolution = (byte)(((i & 0x0001) ||
        ((plci->B3_prot == 4) && (((byte)(GET_WORD((byte   *)b3_config_parms[1].info))) != 5))) ? T30_RESOLUTION_R8_0770_OR_200 : 0);
      ((T30_INFO *)&nlc[1])->data_format = (byte)(GET_WORD((byte   *)b3_config_parms[1].info));
      fax_control_bits = T30_CONTROL_BIT_ALL_FEATURES;
      if ((((T30_INFO *)&nlc[1])->rate_div_2400 != 0) && (((T30_INFO *)&nlc[1])->rate_div_2400 <= 6))
        fax_control_bits &= ~T30_CONTROL_BIT_ENABLE_V34FAX;
      if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_FAX_PAPER_FORMATS)
      {

        if ((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[plci->appl->Id-1])
          & (1L << PRIVATE_FAX_PAPER_FORMATS))
        {
          ((T30_INFO *)&nlc[1])->resolution |= T30_RESOLUTION_R8_1540 |
            T30_RESOLUTION_R16_1540_OR_400 | T30_RESOLUTION_300_300 |
            T30_RESOLUTION_INCH_BASED | T30_RESOLUTION_METRIC_BASED;
        }

 ((T30_INFO *)&nlc[1])->recording_properties =
   T30_RECORDING_WIDTH_ISO_A3 |
   (T30_RECORDING_LENGTH_UNLIMITED << 2) |
   (T30_MIN_SCANLINE_TIME_00_00_00 << 4);
      }
      if(plci->B3_prot == 5)
      {
        if (i & 0x0002) /* Accept incoming fax-polling requests */
          fax_control_bits |= T30_CONTROL_BIT_ACCEPT_POLLING;
        if (i & 0x2000) /* Do not use MR compression */
          fax_control_bits &= ~T30_CONTROL_BIT_ENABLE_2D_CODING;
        if (i & 0x4000) /* Do not use MMR compression */
          fax_control_bits &= ~T30_CONTROL_BIT_ENABLE_T6_CODING;
        if (i & 0x8000) /* Do not use ECM */
          fax_control_bits &= ~T30_CONTROL_BIT_ENABLE_ECM;
        if (plci->fax_connect_info_length != 0)
        {
          ((T30_INFO *)&nlc[1])->resolution = ((T30_INFO   *)plci->fax_connect_info_buffer)->resolution;
          ((T30_INFO *)&nlc[1])->data_format = ((T30_INFO   *)plci->fax_connect_info_buffer)->data_format;
          ((T30_INFO *)&nlc[1])->recording_properties = ((T30_INFO   *)plci->fax_connect_info_buffer)->recording_properties;
          fax_control_bits |= GET_WORD(&((T30_INFO   *)plci->fax_connect_info_buffer)->control_bits_low) &
            (T30_CONTROL_BIT_REQUEST_POLLING | T30_CONTROL_BIT_MORE_DOCUMENTS);
        }
      }
      /* copy station id to NLC */
      for(i=0; i<20; i++)
      {
        if(i<b3_config_parms[2].length)
        {
          ((T30_INFO *)&nlc[1])->station_id[i] = ((byte   *)b3_config_parms[2].info)[1+i];
        }
        else
        {
          ((T30_INFO *)&nlc[1])->station_id[i] = ' ';
        }
      }
      ((T30_INFO *)&nlc[1])->station_id_len = 20;
      /* copy head line to NLC */
      if(b3_config_parms[3].length)
      {

        pos = (byte)(fax_head_line_time (&(((T30_INFO *)&nlc[1])->station_id[20])));
        if (pos != 0)
        {
          if (CAPI_MAX_DATE_TIME_LENGTH + 2 + b3_config_parms[3].length > CAPI_MAX_HEAD_LINE_SPACE)
            pos = 0;
          else
          {
            ((T30_INFO *)&nlc[1])->station_id[20 + pos++] = ' ';
            ((T30_INFO *)&nlc[1])->station_id[20 + pos++] = ' ';
            len = (byte)b3_config_parms[2].length;
            if (len > 20)
              len = 20;
            if (CAPI_MAX_DATE_TIME_LENGTH + 2 + len + 2 + b3_config_parms[3].length <= CAPI_MAX_HEAD_LINE_SPACE)
            {
              for (i = 0; i < len; i++)
                ((T30_INFO *)&nlc[1])->station_id[20 + pos++] = ((byte   *)b3_config_parms[2].info)[1+i];
              ((T30_INFO *)&nlc[1])->station_id[20 + pos++] = ' ';
              ((T30_INFO *)&nlc[1])->station_id[20 + pos++] = ' ';
            }
          }
        }

        len = (byte)b3_config_parms[3].length;
        if (len > CAPI_MAX_HEAD_LINE_SPACE - pos)
          len = (byte)(CAPI_MAX_HEAD_LINE_SPACE - pos);
        ((T30_INFO *)&nlc[1])->head_line_len = (byte)(pos + len);
        nlc[0] += (byte)(pos + len);
        for (i = 0; i < len; i++)
          ((T30_INFO *)&nlc[1])->station_id[20 + pos++] = ((byte   *)b3_config_parms[3].info)[1+i];
        }
      else
        ((T30_INFO *)&nlc[1])->head_line_len = 0;

      plci->nsf_control_bits = 0;
      if(plci->B3_prot == 5)
      {
        if ((plci->adapter->man_profile.private_options & (1L << PRIVATE_FAX_SUB_SEP_PWD))
         && (GET_WORD((byte   *)b3_config_parms[1].info) & 0x8000)) /* Private SUB/SEP/PWD enable */
        {
          plci->requested_options |= 1L << PRIVATE_FAX_SUB_SEP_PWD;
        }
        if ((plci->adapter->man_profile.private_options & (1L << PRIVATE_FAX_NONSTANDARD))
         && (GET_WORD((byte   *)b3_config_parms[1].info) & 0x4000)) /* Private non-standard facilities enable */
        {
          plci->requested_options |= 1L << PRIVATE_FAX_NONSTANDARD;
        }
        if ((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[plci->appl->Id-1])
          & ((1L << PRIVATE_FAX_SUB_SEP_PWD) | (1L << PRIVATE_FAX_NONSTANDARD)))
        {
        if ((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[plci->appl->Id-1])
          & (1L << PRIVATE_FAX_SUB_SEP_PWD))
        {
          fax_control_bits |= T30_CONTROL_BIT_ACCEPT_SUBADDRESS | T30_CONTROL_BIT_ACCEPT_PASSWORD;
          if (fax_control_bits & T30_CONTROL_BIT_ACCEPT_POLLING)
            fax_control_bits |= T30_CONTROL_BIT_ACCEPT_SEL_POLLING;
          }
            len = nlc[0];
          pos = ((byte)(((T30_INFO *) 0)->station_id + 20));
   if (pos < plci->fax_connect_info_length)
   {
     for (i = 1 + plci->fax_connect_info_buffer[pos]; i != 0; i--)
              nlc[++len] = plci->fax_connect_info_buffer[pos++];
          }
   else
     nlc[++len] = 0;
   if (pos < plci->fax_connect_info_length)
   {
     for (i = 1 + plci->fax_connect_info_buffer[pos]; i != 0; i--)
              nlc[++len] = plci->fax_connect_info_buffer[pos++];
          }
   else
     nlc[++len] = 0;
          if ((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[plci->appl->Id-1])
            & (1L << PRIVATE_FAX_NONSTANDARD))
          {
     if ((pos < plci->fax_connect_info_length) && (plci->fax_connect_info_buffer[pos] != 0))
     {
              if ((plci->fax_connect_info_buffer[pos] >= 3) && (plci->fax_connect_info_buffer[pos+1] >= 2))
                plci->nsf_control_bits = GET_WORD(&plci->fax_connect_info_buffer[pos+2]);
       for (i = 1 + plci->fax_connect_info_buffer[pos]; i != 0; i--)
                nlc[++len] = plci->fax_connect_info_buffer[pos++];
            }
     else
     {
              if(api_parse(&b3_config->info[1], (word)b3_config->length, "wwsss", b3_config_parms))
              {
                dbug(1,dprintf("non-standard facilities info missing or wrong format"));
                nlc[++len] = 0;
              }
       else
       {
                if ((b3_config_parms[4].length >= 3) && (b3_config_parms[4].info[1] >= 2))
                  plci->nsf_control_bits = GET_WORD(&b3_config_parms[4].info[2]);
         nlc[++len] = (byte)(b3_config_parms[4].length);
         for (i = 0; i < b3_config_parms[4].length; i++)
    nlc[++len] = b3_config_parms[4].info[1+i];
       }
            }
          }
            nlc[0] = len;
   if ((plci->nsf_control_bits & T30_NSF_CONTROL_BIT_ENABLE_NSF)
    && (plci->nsf_control_bits & T30_NSF_CONTROL_BIT_NEGOTIATE_RESP))
   {
            ((T30_INFO *)&nlc[1])->operating_mode = T30_OPERATING_MODE_CAPI_NEG;
          }
        }
      }

      PUT_WORD(&(((T30_INFO *)&nlc[1])->control_bits_low), fax_control_bits);
      len = ((byte)(((T30_INFO *) 0)->station_id + 20));
      for (i = 0; i < len; i++)
        plci->fax_connect_info_buffer[i] = nlc[1+i];
      ((T30_INFO   *) plci->fax_connect_info_buffer)->head_line_len = 0;
      i += ((T30_INFO *)&nlc[1])->head_line_len;
      while (i < nlc[0])
        plci->fax_connect_info_buffer[len++] = nlc[++i];
      plci->fax_connect_info_length = len;
    }
    else
    {
      nlc[0] = 14;
      if(b3_config->length!=16)
        return _B3_PARM_NOT_SUPPORTED;
      for(i=0; i<12; i++) nlc[1+i] = b3_config->info[1+i];
      if(GET_WORD(&b3_config->info[13])!=8 && GET_WORD(&b3_config->info[13])!=128)
        return _B3_PARM_NOT_SUPPORTED;
      nlc[13] = b3_config->info[13];
      if(GET_WORD(&b3_config->info[15])>=nlc[13])
        return _B3_PARM_NOT_SUPPORTED;
      nlc[14] = b3_config->info[15];
    }
  }
  else
  {
    if (plci->B3_prot == 4 
     || plci->B3_prot == 5 /*T.30 - FAX*/ ) return _B3_PARM_NOT_SUPPORTED;
  }
  add_p(plci, NLC, nlc);
  return 0;
}

/*----------------------------------------------------------------*/
/*      make the same as add_b23, but only for the modem related  */
/*      L2 and L3 B-Chan protocol.                                */
/*                                                                */
/*      Enabled L2 and L3 Configurations:                         */
/*        If L1 == Modem all negotiation                          */
/*          only L2 == Modem with full negotiation is allowed     */
/*        If L1 == Modem async or sync                            */
/*          only L2 == Transparent is allowed                     */
/*        L3 == Modem or L3 == Transparent are allowed            */
/*      B2 Configuration for modem:                               */
/*          word : enable/disable compression, bitoptions         */
/*      B3 Configuration for modem:                               */
/*          empty                                                 */
/*----------------------------------------------------------------*/
static word add_modem_b23 (PLCI  * plci, API_PARSE* bp_parms)
{
  static byte lli[12] = {1,1};
  static byte llc[3] = {2,0,0};
  static byte dlc[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    API_PARSE mdm_config[2];
  word i;
  word b2_config = 0;

  for(i=0;i<2;i++) mdm_config[i].length = 0;
  for(i=0;i<sizeof(dlc);i++) dlc[i] = 0;

  if (((GET_WORD(bp_parms[0].info) == B1_MODEM_ALL_NEGOTIATE)
    && (GET_WORD(bp_parms[1].info) != B2_MODEM_EC_COMPRESSION))
   || ((GET_WORD(bp_parms[0].info) != B1_MODEM_ALL_NEGOTIATE)
    && (GET_WORD(bp_parms[1].info) != B2_TRANSPARENT)))
  {
    return (_B_STACK_NOT_SUPPORTED);
  }
  if ((GET_WORD(bp_parms[2].info) != B3_MODEM)
   && (GET_WORD(bp_parms[2].info) != B3_TRANSPARENT))
  {
    return (_B_STACK_NOT_SUPPORTED);
  }

  plci->B2_prot = (byte) GET_WORD(bp_parms[1].info);
  plci->B3_prot = (byte) GET_WORD(bp_parms[2].info);

  if ((GET_WORD(bp_parms[1].info) == B2_MODEM_EC_COMPRESSION) && bp_parms[4].length)
  {
    if (api_parse (&bp_parms[4].info[1],
                  (word)bp_parms[4].length, "w",
                  mdm_config))
    {
      return (_WRONG_MESSAGE_FORMAT);
    }
    b2_config = GET_WORD(mdm_config[0].info);
  }

  /* OK, L2 is modem */

  lli[0] = 1;
  lli[1] = 1;
  if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_XONOFF_FLOW_CONTROL)
    lli[1] |= 2;
  if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_OOB_CHANNEL)
    lli[1] |= 4;

  if ((lli[1] & 0x02) && (diva_xdi_extended_features & DIVA_CAPI_USE_CMA)) {
    lli[1] |= 0x10;
    if (plci->rx_dma_descriptor <= 0) {
      plci->rx_dma_descriptor=diva_get_dma_descriptor(plci,&plci->rx_dma_magic);
      if (plci->rx_dma_descriptor >= 0)
        plci->rx_dma_descriptor++;
    }
    if (plci->rx_dma_descriptor > 0) {
      lli[1] |= 0x40;
      lli[0] = 6;
      lli[2] = (byte)(plci->rx_dma_descriptor - 1);
      lli[3] = (byte)plci->rx_dma_magic;
      lli[4] = (byte)(plci->rx_dma_magic >>  8);
      lli[5] = (byte)(plci->rx_dma_magic >> 16);
      lli[6] = (byte)(plci->rx_dma_magic >> 24);
    }
  }

  if (DIVA_CAPI_SUPPORTS_NO_CANCEL(plci->adapter)) {
    lli[1] |= 0x20;
  }

  llc[1] = (plci->call_dir & (CALL_DIR_ORIGINATE | CALL_DIR_FORCE_OUTG_NL)) ?
    /*V42*/ 10 : /*V42_IN*/ 9;
  llc[2] = 4;                      /* pass L3 always transparent */
  add_p(plci, LLI, lli);
  add_p(plci, LLC, llc);
  i =  1;
  PUT_WORD (&dlc[i], plci->appl->MaxDataLength);
  i += 2;
  if (GET_WORD(bp_parms[1].info) == B2_MODEM_EC_COMPRESSION)
  {
    if (bp_parms[4].length)
  {
    dbug(1, dprintf("MDM b2_config=%02x", b2_config));
    dlc[i++] = 3; /* Addr A */
    dlc[i++] = 1; /* Addr B */
    dlc[i++] = 7; /* modulo mode */
    dlc[i++] = 7; /* window size */
    dlc[i++] = 0; /* XID len Lo  */
    dlc[i++] = 0; /* XID len Hi  */

    if (b2_config & MDM_B2_DISABLE_V42bis)
    {
      dlc[i] |= DLC_MODEMPROT_DISABLE_V42_V42BIS;
    }
    if (b2_config & MDM_B2_DISABLE_MNP)
    {
      dlc[i] |= DLC_MODEMPROT_DISABLE_MNP_MNP5;
    }
    if (b2_config & MDM_B2_DISABLE_TRANS)
    {
      dlc[i] |= DLC_MODEMPROT_REQUIRE_PROTOCOL;
    }
    if (b2_config & MDM_B2_DISABLE_V42)
    {
      dlc[i] |= DLC_MODEMPROT_DISABLE_V42_DETECT;
    }
    if (b2_config & MDM_B2_DISABLE_COMP)
    {
      dlc[i] |= DLC_MODEMPROT_DISABLE_COMPRESSION;
    }
    i++;
  }
  }
  else
  {
    dlc[i++] = 3; /* Addr A */
    dlc[i++] = 1; /* Addr B */
    dlc[i++] = 7; /* modulo mode */
    dlc[i++] = 7; /* window size */
    dlc[i++] = 0; /* XID len Lo  */
    dlc[i++] = 0; /* XID len Hi  */
    dlc[i++] = DLC_MODEMPROT_DISABLE_V42_V42BIS |
               DLC_MODEMPROT_DISABLE_MNP_MNP5 |
               DLC_MODEMPROT_DISABLE_V42_DETECT |
               DLC_MODEMPROT_DISABLE_COMPRESSION;
  }
  dlc[0] = (byte)(i - 1);
/* HexDump ("DLC", sizeof(dlc), &dlc[0]); */
  add_p(plci, DLC, dlc);
  return (0);
}


/*------------------------------------------------------------------*/
/* send a request for the signaling entity                          */
/*------------------------------------------------------------------*/

void sig_req(PLCI   * plci, byte req, byte Id)
{
  if(!plci) return;
  if(plci->adapter->adapter_disabled) return;
  dbug(1,dprintf("sig_req(%x)",req));
  if (req == REMOVE)
    plci->sig_remove_id = plci->Sig.Id;
  if(plci->req_in==plci->req_in_start) {
    plci->req_in +=2;
    plci->RBuffer[plci->req_in++] = 0;
  }
  PUT_WORD(&plci->RBuffer[plci->req_in_start], plci->req_in-plci->req_in_start-2);
  plci->RBuffer[plci->req_in++] = Id;   /* sig/nl flag */
  plci->RBuffer[plci->req_in++] = req;  /* request */
  plci->RBuffer[plci->req_in++] = 0;    /* channel */
  plci->req_in_start = plci->req_in;
}

/*------------------------------------------------------------------*/
/* send a request for the network layer entity                      */
/*------------------------------------------------------------------*/

static void nl_req_ncci(PLCI *plci, byte req, byte ncci)
{
  if(!plci) return;
  if(plci->adapter->adapter_disabled) return;
  dbug(1,dprintf("nl_req %02x %02x %02x", plci->Id, req, ncci));
  if (req == REMOVE)
  {
    plci->nl_remove_id = plci->NL.Id;
    ncci_remove (plci, 0, (byte)(ncci != 0));
    ncci = 0;
  }
  if(plci->req_in==plci->req_in_start) {
    plci->req_in +=2;
    plci->RBuffer[plci->req_in++] = 0;
  }
  PUT_WORD(&plci->RBuffer[plci->req_in_start], plci->req_in-plci->req_in_start-2);
  plci->RBuffer[plci->req_in++] = 1;    /* sig/nl flag */
  plci->RBuffer[plci->req_in++] = req;  /* request */
  plci->RBuffer[plci->req_in++] = plci->adapter->ncci_ch[ncci];   /* channel */
  plci->req_in_start = plci->req_in;
}

static void send_req(PLCI *plci)
{
  ENTITY   * e;
  word l;
/*  word i; */

  if(!plci) return;
  if(plci->adapter->adapter_disabled) return;
  channel_xmit_xon (plci);

        /* if nothing to do, return */
  if(plci->req_in==plci->req_out) return;
  dbug(1,dprintf("send_req(in=%d,out=%d)",plci->req_in,plci->req_out));

  if(plci->nl_req || plci->sig_req) return;

  l = GET_WORD(&plci->RBuffer[plci->req_out]);
  plci->req_out += 2;
  plci->XData[0].P = &plci->RBuffer[plci->req_out];
  plci->req_out += l;
  if(plci->RBuffer[plci->req_out]==1)
  {
    e = &plci->NL;
    plci->req_out++;
    e->Req = plci->nl_req = plci->RBuffer[plci->req_out++];
    e->ReqCh = plci->RBuffer[plci->req_out++];
    if(!(e->Id & 0x1f))
    {
      e->Id = NL_ID;
      plci->RBuffer[plci->req_out-4] = CAI;
      plci->RBuffer[plci->req_out-3] = 1;
      plci->RBuffer[plci->req_out-2] = (plci->Sig.Id==0xff) ? 0 : plci->Sig.Id;
      plci->RBuffer[plci->req_out-1] = 0;
      l+=3;
      plci->nl_global_req = plci->nl_req;
    }
    dbug(1,dprintf("%x:NLREQ(%x:%x:%x)",plci->adapter->Id,e->Id,e->Req,e->ReqCh));
  }
  else
  {
    e = &plci->Sig;
    if(plci->RBuffer[plci->req_out])
      e->Id = plci->RBuffer[plci->req_out];
    plci->req_out++;
    e->Req = plci->sig_req = plci->RBuffer[plci->req_out++];
    e->ReqCh = plci->RBuffer[plci->req_out++];
    if(!(e->Id & 0x1f))
      plci->sig_global_req = plci->sig_req;
    dbug(1,dprintf("%x:SIGREQ(%x:%x:%x)",plci->adapter->Id,e->Id,e->Req,e->ReqCh));
  }
  plci->XData[0].PLength = l;
  e->X = plci->XData;
  plci->adapter->request(e);
  dbug(1,dprintf("send_ok"));
}

void send_data(PLCI   * plci)
{
  DIVA_CAPI_ADAPTER   * a;
  DATA_B3_DESC   * data;
  NCCI   *ncci_ptr;
  word ncci;

  if (!plci->nl_req && plci->ncci_ring_list)
  {
    a = plci->adapter;
    ncci = plci->ncci_ring_list;
    do
    {
      ncci = a->ncci_next[ncci];
      ncci_ptr = &(a->ncci[ncci]);
      if (!(a->ncci_ch[ncci]
         && (a->ch_flow_control[a->ncci_ch[ncci]] & N_OK_FC_PENDING)))
      {
        if (ncci_ptr->data_pending)
        {
          if ((a->ncci_state[ncci] == CONNECTED)
           || (a->ncci_state[ncci] == INC_ACT_PENDING)
           || (plci->send_disc == ncci))
          {
            data = &(ncci_ptr->DBuffer[ncci_ptr->data_out]);
            if ((plci->B2_prot == B2_V120_ASYNC)
             || (plci->B2_prot == B2_V120_ASYNC_V42BIS)
             || (plci->B2_prot == B2_V120_BIT_TRANSPARENT))
            {
              plci->NData[1].P = TransmitBufferGet (plci->appl, data->P);
              plci->NData[1].PLength = data->Length;
              if (data->Flags & 0x10)
                plci->NData[0].P = v120_break_header;
              else
                plci->NData[0].P = v120_default_header;
              plci->NData[0].PLength = 1 ;
              plci->NL.XNum = 2;
              plci->NL.Req = plci->nl_req = (byte)((data->Flags&0x07)<<4 |N_DATA);
            }
            else
            {
              plci->NData[0].P = TransmitBufferGet (plci->appl, data->P);
              plci->NData[0].PLength = data->Length;
              if (data->Flags & 0x10)
                plci->NL.Req = plci->nl_req = (byte)N_UDATA;

              else if ((plci->B3_prot == B3_RTP) && (data->Flags & 0x01))
                plci->NL.Req = plci->nl_req = (byte)N_BDATA;

              else
                plci->NL.Req = plci->nl_req = (byte)((data->Flags&0x07)<<4 |N_DATA);
            }
            plci->NL.X = plci->NData;
            plci->NL.ReqCh = a->ncci_ch[ncci];
            dbug(1,dprintf("%x:DREQ(%x:%x)",a->Id,plci->NL.Id,plci->NL.Req));
            plci->data_sent = true;
            plci->data_sent_ptr = data->P;
            a->request(&plci->NL);
          }
          else {
            cleanup_ncci_data (plci, ncci);
          }
        }
        else if (plci->send_disc == ncci)
        {
          /* dprintf("N_DISC"); */
          plci->NData[0].PLength = 0;
          plci->NL.ReqCh = a->ncci_ch[ncci];
          plci->NL.Req = plci->nl_req = N_DISC;
          a->request(&plci->NL);
          plci->command = _DISCONNECT_B3_R;
          plci->send_disc = 0;
        }
      }
    } while (!plci->nl_req && (ncci != plci->ncci_ring_list));
    plci->ncci_ring_list = ncci;
  }
}

static void listen_check(DIVA_CAPI_ADAPTER *a)
{
  word i,j;
  PLCI   * plci;
  byte activnotifiedcalls = 0;

  dbug(1,dprintf("listen_check(%d,%d)",a->listen_active,a->max_listen));
  if (!remove_started && !a->adapter_disabled)
  {
    for(i=0;i<a->max_plci;i++)
    {
      plci = &(a->plci[i]);
      if(plci->notifiedcall) activnotifiedcalls++;
    }
    dbug(1,dprintf("listen_check(%d)",activnotifiedcalls));

    for(i=a->listen_active; i < ((word)(a->max_listen+activnotifiedcalls)); i++) {
      if((j=get_plci(a))) {
        a->listen_active++;
        plci = &a->plci[j-1];
        plci->State = LISTENING;

        add_p(plci,OAD,"\x01\xfd");

        add_p(plci,KEY,"\x04\x43\x41\x32\x30");

        add_p(plci,CAI,"\x01\xc0");
        add_p(plci,UID,"\x06\x43\x61\x70\x69\x32\x30");
        add_p(plci,LLI,"\x01\xc4");                  /* support Dummy CR FAC + MWI + SpoofNotify */       
        add_p(plci,SHIFT|6,NULL);
        add_p(plci,SIN,"\x02\x00\x00");
        plci->internal_command = LISTEN_SIG_ASSIGN_PEND;     /* do indicate_req if OK  */
        sig_req(plci,ASSIGN,DSIG_ID);
        send_req(plci);
      }
    }
  }
}

/*------------------------------------------------------------------*/
/* functions for all parameters sent in INDs                        */
/*------------------------------------------------------------------*/

static void IndParse(PLCI *plci, word *parms_id, byte **parms, byte multiIEsize)
{
  word ploc;            /* points to current location within packet */
  byte w;
  byte wlen;
  byte codeset,lock;
  byte   * in;
  word i;
  word code;
  word mIEindex = 0;
  ploc = 0;
  codeset = 0;
  lock = 0;

  in = plci->Sig.RBuffer->P;
  for(i=0; i<parms_id[0]; i++)   /* multiIE parms_id contains just the 1st */
  {                            /* element but parms array is larger      */
    parms[i] = (byte   *)"";
  }
  for(i=0; i<multiIEsize; i++)
  {
    parms[i] = (byte   *)"";
  }

  while(ploc<plci->Sig.RBuffer->length-1) {

        /* read information element id and length                   */
    w = in[ploc];

    if(w & 0x80) {
/*    w &=0xf0; removed, cannot detect congestion levels */
/*    upper 4 bit masked with w==SHIFT now               */
      wlen = 0;
    }
    else {
      wlen = (byte)(in[ploc+1]+1);
    }
        /* check if length valid (not exceeding end of packet)      */
    if((ploc+wlen) > 270) return ;
    if(lock & 0x80) lock &=0x7f;
    else codeset = lock;

    if((w&0xf0)==SHIFT) {
      codeset = in[ploc];
      if(!(codeset & 0x08)) lock = (byte)(codeset & 7);
      codeset &=7;
      lock |=0x80;
    }
    else {
      if(w==ESC && wlen>=3) code = in[ploc+2] |0x800;
      else code = w;
      code |= (codeset<<8);

      for(i=1; i<parms_id[0]+1 && parms_id[i]!=code; i++);

      if(i<parms_id[0]+1) {
        if(!multiIEsize) { /* with multiIEs use next field index,          */
          mIEindex = i-1;    /* with normal IEs use same index like parms_id */
        }

        parms[mIEindex] = &in[ploc+1];
        dbug(1,dprintf("mIE[%d]=0x%x",*parms[mIEindex],in[ploc]));
        if(parms_id[i]==OAD
        || parms_id[i]==CONN_NR
        || parms_id[i]==CAD) {
          if(in[ploc+2] &0x80) {
            in[ploc+0] = (byte)(in[ploc+1]+1);
            in[ploc+1] = (byte)(in[ploc+2] &0x7f);
            in[ploc+2] = 0x80;
            parms[mIEindex] = &in[ploc];
          }
        }
        mIEindex++;       /* effects multiIEs only */
      }
    }

    ploc +=(wlen+1);
  }
  return ;
}

/*------------------------------------------------------------------*/
/* try to match a cip from received BC and HLC                      */
/*------------------------------------------------------------------*/

static byte ie_compare(byte *ie1, byte *ie2)
{
  word i;
  if(!ie1 || ! ie2) return false;
  if(!ie1[0]) return false;
  for(i=0;i<(word)(ie1[0]+1);i++) if(ie1[i]!=ie2[i]) return false;
  return true;
}

static word find_cip(DIVA_CAPI_ADAPTER *a, byte *bc, byte *hlc)
{
  word i;
  word j;

  for(i=9;i && !ie_compare(bc,cip_bc[i][a->u_law]);i--);

  for(j=16;j<29 &&
           (!ie_compare(bc,cip_bc[j][a->u_law]) || !ie_compare(hlc,cip_hlc[j])); j++);
  if(j==29) return i;
  return j;
}


static byte AddInfo(byte   **add_i,
                    byte   **fty_i,
                    byte   *esc_chi,
                    byte *facility)
{
  byte i;
  byte j;
  byte k;
  byte flen;
  byte len=0;
   /* facility is a nested structure */
   /* FTY can be more than once      */

	if (esc_chi[0] && !(esc_chi[esc_chi[0]] & 0x7f))
  {
    add_i[0] = (byte   *)"\x02\x02\x00"; /* use neither b nor d channel */
  }

  else
  {
    add_i[0] = (byte   *)"";
  }
  if(!fty_i[0][0])
  {
    add_i[3] = (byte   *)"";
  }
  else
  {    /* facility array found  */
    for(i=0,j=1;i<MAX_MULTI_IE && fty_i[i][0];i++)
    {
      dbug(1,dprintf("AddIFac[%d]",fty_i[i][0]));
      len += fty_i[i][0];
      len += 2;
      flen=fty_i[i][0];
      facility[j++]=0x1c; /* copy fac IE */
      for(k=0;k<=flen;k++,j++)
      {
        facility[j]=fty_i[i][k];
/*      dbug(1,dprintf("%x ",facility[j])); */
      }
    }
    facility[0] = len;
    add_i[3] = facility;
  }
/*  dbug(1,dprintf("FacArrLen=%d ",len)); */
  len = add_i[0][0]+add_i[1][0]+add_i[2][0]+add_i[3][0];
  len += 4;                          /* calculate length of all */
  return(len);
}

/*------------------------------------------------------------------*/
/* voice and codec features                                         */
/*------------------------------------------------------------------*/

static void SetVoiceChannel(PLCI *plci, byte *chi, DIVA_CAPI_ADAPTER *a)
{
  byte voice_chi[] = "\x02\x18\x01";
  byte channel;

  channel = chi[chi[0]]&0x3;
  dbug(1,dprintf("ExtDevON(Ch=0x%x)",channel));
  voice_chi[2] = (channel) ? channel : 1;
  add_p(plci,FTY,"\x02\x01\x07");             /* B On, default on 1 */
  add_p(plci,ESC,voice_chi);                  /* Channel */
  sig_req(plci,TEL_CTRL,0);
  send_req(plci);
  if(a->AdvSignalPLCI)
  {
    adv_voice_write_coefs (a->AdvSignalPLCI, ADV_VOICE_WRITE_ACTIVATION);
  }
}

static void VoiceChannelOff(PLCI *plci)
{
  dbug(1,dprintf("ExtDevOFF"));
  add_p(plci,FTY,"\x02\x01\x08");             /* B Off */
  sig_req(plci,TEL_CTRL,0);
  send_req(plci);
  if(plci->adapter->AdvSignalPLCI)
  {
    adv_voice_clear_config (plci->adapter->AdvSignalPLCI);
  }
}


static word AdvCodecSupport(DIVA_CAPI_ADAPTER *a, PLCI *plci, APPL *appl,
			    byte hook_listen)
{
  word j;
  PLCI   *splci;

  /* check if hardware supports handset with hook states (adv.codec) */
  /* or if just a on board codec is supported                        */
  /* the advanced codec plci is just for internal use                */

  /* diva Pro with on-board codec:                                   */
  if(a->profile.Global_Options & HANDSET)
  {
    /* new call, but hook states are already signalled */
    if(a->AdvCodecFLAG)
    {
      if(a->AdvSignalAppl!=appl || a->AdvSignalPLCI)
      {
        dbug(1,dprintf("AdvSigPlci=0x%x",a->AdvSignalPLCI));
        return 0x2001; /* codec in use by another application */
      }
      if(plci!=NULL)
      {
        a->AdvSignalPLCI = plci;
        plci->tel=ADV_VOICE;
      }
      return 0;                      /* adv codec still used */
    }
    if((j=get_plci(a)))
    {
      splci = &a->plci[j-1];
      splci->tel = CODEC_PERMANENT;
      /* hook_listen indicates if a facility_req with handset/hook support */
      /* was sent. Otherwise if just a call on an external device was made */
      /* the codec will be used but the hook info will be discarded (just  */
      /* the external controller is in use                                 */
      if(hook_listen) splci->State = ADVANCED_VOICE_SIG;
      else
      {
        splci->State = ADVANCED_VOICE_NOSIG;
        if(plci)
        {
          plci->spoofed_msg = SPOOFING_REQUIRED;
        }
                                               /* indicate D-ch connect if  */
      }                                        /* codec is connected OK     */
      if(plci!=NULL)
      {
        a->AdvSignalPLCI = plci;
        plci->tel=ADV_VOICE;
      }
      a->AdvSignalAppl = appl;
      a->AdvCodecFLAG = true;
      a->AdvCodecPLCI = splci;
      add_p(splci,CAI,"\x01\x15");
      add_p(splci,LLI,"\x01\x00");
      add_p(splci,ESC,"\x02\x18\x00");
      add_p(splci,UID,"\x06\x43\x61\x70\x69\x32\x30");
      splci->internal_command = PERM_COD_ASSIGN;
      dbug(1,dprintf("Codec Assign"));
      sig_req(splci,ASSIGN,DSIG_ID);
      send_req(splci);
    }
    else
    {
      return 0x2001; /* wrong state, no more plcis */
    }
  }
  else if(a->profile.Global_Options & ON_BOARD_CODEC)
  {
    if(hook_listen) return 0x300B;               /* Facility not supported */
                                                 /* no hook with SCOM      */
    if(plci!=NULL) plci->tel = CODEC;
    dbug(1,dprintf("S/SCOM codec"));
    /* first time we use the scom-s codec we must shut down the internal   */
    /* handset application of the card. This can be done by an assign with */
    /* a cai with the 0x80 bit set. Assign return code is 'out of resource'*/
    if(!a->scom_appl_disable){
      if((j=get_plci(a))) {
        splci = &a->plci[j-1];
        add_p(splci,CAI,"\x01\x80");
        add_p(splci,UID,"\x06\x43\x61\x70\x69\x32\x30");
        sig_req(splci,ASSIGN,0xC0);  /* 0xc0 is the TEL_ID */
        send_req(splci);
        a->scom_appl_disable = true;
      }
      else{
        return 0x2001; /* wrong state, no more plcis */
      }
    }
  }
  else return 0x300B;               /* Facility not supported */

  return 0;
}


static void CodecIdCheck(DIVA_CAPI_ADAPTER *a, PLCI *plci)
{

  dbug(1,dprintf("CodecIdCheck"));

  if(a->AdvSignalPLCI == plci)
  {
    dbug(1,dprintf("PLCI owns codec"));
    VoiceChannelOff(a->AdvCodecPLCI);
    if(a->AdvCodecPLCI->State == ADVANCED_VOICE_NOSIG)
    {
      dbug(1,dprintf("remove temp codec PLCI"));
      plci_remove(a->AdvCodecPLCI);
      a->AdvCodecFLAG  = 0;
      a->AdvCodecPLCI  = NULL;
      a->AdvSignalAppl = NULL;
    }
    a->AdvSignalPLCI = NULL;
  }
}

/* -------------------------------------------------------------------
    Ask for physical address of card on PCI bus
   ------------------------------------------------------------------- */
static void diva_ask_for_xdi_sdram_bar (DIVA_CAPI_ADAPTER  * a,
                                        IDI_SYNC_REQ  * preq) {
  a->sdram_bar = 0;
  if (diva_xdi_extended_features & DIVA_CAPI_XDI_PROVIDES_SDRAM_BAR) {
    ENTITY   * e = (ENTITY   *)preq;

    e->user[0] = a->Id - 1;
    preq->xdi_sdram_bar.info.bar    = 0;
    preq->xdi_sdram_bar.Req         = 0;
    preq->xdi_sdram_bar.Rc           = IDI_SYNC_REQ_XDI_GET_ADAPTER_SDRAM_BAR;

    (*(a->request))(e);

    a->sdram_bar = preq->xdi_sdram_bar.info.bar;
    dbug(3,dprintf("A(%d) SDRAM BAR = %08x", a->Id, a->sdram_bar));
  }
}

/* -------------------------------------------------------------------
     Ask XDI about extended features
   ------------------------------------------------------------------- */
static void diva_get_extended_adapter_features (DIVA_CAPI_ADAPTER  * a) {
  IDI_SYNC_REQ   * preq;
    char buffer[              ((sizeof(preq->xdi_extended_features)+4) > sizeof(ENTITY)) ?                     (sizeof(preq->xdi_extended_features)+4) : sizeof(ENTITY)];

    char features[4];
  preq = (IDI_SYNC_REQ   *)&buffer[0];

  if (!diva_xdi_extended_features) {
    ENTITY   * e = (ENTITY   *)preq;
    diva_xdi_extended_features |= 0x80000000;

    e->user[0] = a->Id - 1;
    preq->xdi_extended_features.Req = 0;
    preq->xdi_extended_features.Rc  = IDI_SYNC_REQ_XDI_GET_EXTENDED_FEATURES;
    preq->xdi_extended_features.info.buffer_length_in_bytes = sizeof(features);
    preq->xdi_extended_features.info.features = &features[0];

    (*(a->request))(e);

    if (features[0] & DIVA_XDI_EXTENDED_FEATURES_VALID) {
      /*
         Check features located in the byte '0'
         */
      if (features[0] & DIVA_XDI_EXTENDED_FEATURE_CMA) {
        diva_xdi_extended_features |= DIVA_CAPI_USE_CMA;
      }
      if (features[0] & DIVA_XDI_EXTENDED_FEATURE_RX_DMA) {
        diva_xdi_extended_features |= DIVA_CAPI_XDI_PROVIDES_RX_DMA;
        dbug(1,dprintf("XDI provides RxDMA"));
      }
      if (features[0] & DIVA_XDI_EXTENDED_FEATURE_SDRAM_BAR) {
        diva_xdi_extended_features |= DIVA_CAPI_XDI_PROVIDES_SDRAM_BAR;
      }
      if (features[0] & DIVA_XDI_EXTENDED_FEATURE_NO_CANCEL_RC) {
        diva_xdi_extended_features |= DIVA_CAPI_XDI_PROVIDES_NO_CANCEL;
        dbug(3,dprintf("XDI provides NO_CANCEL_RC feature"));
      }

    }
  }

  diva_ask_for_xdi_sdram_bar (a, preq);
}

/*------------------------------------------------------------------*/
/* automatic law                                                    */
/*------------------------------------------------------------------*/
/* called from OS specific part after init time to get the Law              */
/* a-law (Euro) and u-law (us,japan) use different BCs in the Setup message */
void AutomaticLaw(DIVA_CAPI_ADAPTER   *a)
{
  word j;
  PLCI   *splci;

  if(a->automatic_law) {
    return;
  }
  if((j=get_plci(a))) {
    diva_get_extended_adapter_features (a);
    splci = &a->plci[j-1];
    a->automatic_lawPLCI = splci;
    a->automatic_law = 1;
    add_p(splci,CAI,"\x01\x80");
    add_p(splci,UID,"\x06\x43\x61\x70\x69\x32\x30");
    splci->internal_command = USELAW_REQ;
    splci->command = 0;
    splci->number = 0;
    sig_req(splci,ASSIGN,DSIG_ID);
    send_req(splci);
  }
}

/* called from OS specific part if an application sends an Capi20Release */
word CapiRelease(word Id)
{
  word i, j, appls_found;
  PLCI   *plci;
  APPL   *this;
  DIVA_CAPI_ADAPTER   *a;

  if (!Id)
  {
    dbug(0,dprintf("A: CapiRelease(Id==0)"));
    return (_WRONG_APPL_ID);
  }

  this = &application[Id-1];               /* get application pointer */

  for(i=0,appls_found=0; i<max_appl; i++)
  {
    if(application[i].Id)       /* an application has been found        */
    {
      appls_found++;
    }
  }

  for(i=0; i<max_adapter; i++)             /* scan all adapters...    */
  {
    a = &adapter[i];
    if (a->request)
    {
      a->Info_Mask[Id-1] = 0;
      a->CIP_Mask[Id-1] = 0;
      a->Notification_Mask[Id-1] = 0;
      a->codec_listen[Id-1] = NULL;
      a->requested_options_table[Id-1] = 0;
      for(j=0; j<a->max_plci; j++)           /* and all PLCIs connected */
      {                                      /* with this application   */
        plci = &a->plci[j];
        if(plci->Id)                         /* if plci owns no application */
        {                                    /* it may be not jet connected */
          if(plci->State==INC_CON_PENDING
          || plci->State==INC_CON_ALERT)
          {
            if(test_c_ind_mask_bit (plci, (word)(Id-1)))
            {
              clear_c_ind_mask_bit (plci, (word)(Id-1));
              if(c_ind_mask_empty (plci))
              {
                sig_req(plci,HANGUP,0);
                send_req(plci);
                plci->State = OUTG_DIS_PENDING;
              }
            }
          }
          if(test_c_ind_mask_bit (plci, (word)(Id-1)))
          {
            clear_c_ind_mask_bit (plci, (word)(Id-1));
            if(c_ind_mask_empty (plci))
            {
              if(!plci->appl)
              {
                plci_remove(plci);
                plci->State = IDLE;
              }
            }
          }
          if(plci->appl==this)
          {
            plci->appl = NULL;
            plci_remove(plci);
            plci->State = IDLE;
          }
        }
      }
      listen_check(a);

      if(a->flag_dynamic_l1_down)
      {
        if(appls_found==1)            /* last application does a capi release */
        {
          if((j=get_plci(a)))
          {
            plci = &a->plci[j-1];
            plci->command = 0;
            add_p(plci,OAD,"\x01\xfd");
            add_p(plci,CAI,"\x01\x80");
            add_p(plci,UID,"\x06\x43\x61\x70\x69\x32\x30");
            add_p(plci,SHIFT|6,NULL);
            add_p(plci,SIN,"\x02\x00\x00");
            plci->internal_command = REM_L1_SIG_ASSIGN_PEND;
            sig_req(plci,ASSIGN,DSIG_ID);
            add_p(plci,FTY,"\x02\xff\x06"); /* l1 down */
            sig_req(plci,SIG_CTRL,0);
            send_req(plci);
          }
        }
      }
      if(a->AdvSignalAppl==this)
      {
        this->NullCREnable = false;
        if (a->AdvCodecPLCI)
        {
          plci_remove(a->AdvCodecPLCI);
          a->AdvCodecPLCI->tel = 0;
          a->AdvCodecPLCI->adv_nl = 0;
        }
        a->AdvSignalAppl = NULL;
        a->AdvSignalPLCI = NULL;
        a->AdvCodecFLAG = 0;
        a->AdvCodecPLCI = NULL;
      }
    }
  }

  this->Id = 0;

  return GOOD;
}

static word plci_remove_check(PLCI   *plci)
{
  if(!plci) return true;
  if(!plci->NL.Id && c_ind_mask_empty (plci))
  {
    if(plci->Sig.Id == 0xff)
      plci->Sig.Id = 0;
    if(!plci->Sig.Id)
    {
      dbug(1,dprintf("plci_remove_complete(%x)",plci->Id));
      dbug(1,dprintf("tel=0x%x,Sig=0x%x",plci->tel,plci->Sig.Id));
      if (plci->Id)
      {
        CodecIdCheck(plci->adapter, plci);
        clear_b1_config (plci);
        ncci_remove (plci, 0, false);
        plci_free_msg_in_queue (plci);
        channel_flow_control_remove (plci);
        plci->Id = 0;
        plci->State = IDLE;
        plci->channels = 0;
        plci->appl = NULL;
        plci->notifiedcall = 0;
      }
      listen_check(plci->adapter);
      return true;
    }
  }
  return false;
}


/*------------------------------------------------------------------*/

static byte plci_nl_busy (PLCI   *plci)
{
  /* only applicable for non-multiplexed protocols */
  return (plci->nl_req
    || (plci->ncci_ring_list
     && plci->adapter->ncci_ch[plci->ncci_ring_list]
     && (plci->adapter->ch_flow_control[plci->adapter->ncci_ch[plci->ncci_ring_list]] & N_OK_FC_PENDING)));
}


/*------------------------------------------------------------------*/
/* DTMF facilities                                                  */
/*------------------------------------------------------------------*/


static struct
{
  byte send_mask;
  byte listen_mask;
  byte character;
  byte code;
} dtmf_digit_map[] =
{
  { 0x01, 0x01, 0x23, DTMF_DIGIT_TONE_CODE_HASHMARK },
  { 0x01, 0x01, 0x2a, DTMF_DIGIT_TONE_CODE_STAR },
  { 0x01, 0x01, 0x30, DTMF_DIGIT_TONE_CODE_0 },
  { 0x01, 0x01, 0x31, DTMF_DIGIT_TONE_CODE_1 },
  { 0x01, 0x01, 0x32, DTMF_DIGIT_TONE_CODE_2 },
  { 0x01, 0x01, 0x33, DTMF_DIGIT_TONE_CODE_3 },
  { 0x01, 0x01, 0x34, DTMF_DIGIT_TONE_CODE_4 },
  { 0x01, 0x01, 0x35, DTMF_DIGIT_TONE_CODE_5 },
  { 0x01, 0x01, 0x36, DTMF_DIGIT_TONE_CODE_6 },
  { 0x01, 0x01, 0x37, DTMF_DIGIT_TONE_CODE_7 },
  { 0x01, 0x01, 0x38, DTMF_DIGIT_TONE_CODE_8 },
  { 0x01, 0x01, 0x39, DTMF_DIGIT_TONE_CODE_9 },
  { 0x01, 0x01, 0x41, DTMF_DIGIT_TONE_CODE_A },
  { 0x01, 0x01, 0x42, DTMF_DIGIT_TONE_CODE_B },
  { 0x01, 0x01, 0x43, DTMF_DIGIT_TONE_CODE_C },
  { 0x01, 0x01, 0x44, DTMF_DIGIT_TONE_CODE_D },
  { 0x01, 0x00, 0x61, DTMF_DIGIT_TONE_CODE_A },
  { 0x01, 0x00, 0x62, DTMF_DIGIT_TONE_CODE_B },
  { 0x01, 0x00, 0x63, DTMF_DIGIT_TONE_CODE_C },
  { 0x01, 0x00, 0x64, DTMF_DIGIT_TONE_CODE_D },

  { 0x04, 0x04, 0x80, DTMF_SIGNAL_NO_TONE },
  { 0x00, 0x04, 0x81, DTMF_SIGNAL_UNIDENTIFIED_TONE },
  { 0x04, 0x04, 0x82, DTMF_SIGNAL_DIAL_TONE },
  { 0x04, 0x04, 0x83, DTMF_SIGNAL_PABX_INTERNAL_DIAL_TONE },
  { 0x04, 0x04, 0x84, DTMF_SIGNAL_SPECIAL_DIAL_TONE },
  { 0x04, 0x04, 0x85, DTMF_SIGNAL_SECOND_DIAL_TONE },
  { 0x04, 0x04, 0x86, DTMF_SIGNAL_RINGING_TONE },
  { 0x04, 0x04, 0x87, DTMF_SIGNAL_SPECIAL_RINGING_TONE },
  { 0x04, 0x04, 0x88, DTMF_SIGNAL_BUSY_TONE },
  { 0x04, 0x04, 0x89, DTMF_SIGNAL_CONGESTION_TONE },
  { 0x04, 0x04, 0x8a, DTMF_SIGNAL_SPECIAL_INFORMATION_TONE },
  { 0x04, 0x04, 0x8b, DTMF_SIGNAL_COMFORT_TONE },
  { 0x04, 0x04, 0x8c, DTMF_SIGNAL_HOLD_TONE },
  { 0x04, 0x04, 0x8d, DTMF_SIGNAL_RECORD_TONE },
  { 0x04, 0x04, 0x8e, DTMF_SIGNAL_CALLER_WAITING_TONE },
  { 0x04, 0x04, 0x8f, DTMF_SIGNAL_CALL_WAITING_TONE },
  { 0x04, 0x04, 0x90, DTMF_SIGNAL_PAY_TONE },
  { 0x04, 0x04, 0x91, DTMF_SIGNAL_POSITIVE_INDICATION_TONE },
  { 0x04, 0x04, 0x92, DTMF_SIGNAL_NEGATIVE_INDICATION_TONE },
  { 0x04, 0x04, 0x93, DTMF_SIGNAL_WARNING_TONE },
  { 0x04, 0x04, 0x94, DTMF_SIGNAL_INTRUSION_TONE },
  { 0x04, 0x04, 0x95, DTMF_SIGNAL_CALLING_CARD_SERVICE_TONE },
  { 0x04, 0x04, 0x96, DTMF_SIGNAL_PAYPHONE_RECOGNITION_TONE },
  { 0x04, 0x04, 0x97, DTMF_SIGNAL_CPE_ALERTING_SIGNAL },
  { 0x04, 0x04, 0x98, DTMF_SIGNAL_OFF_HOOK_WARNING_TONE },
  { 0x04, 0x04, 0xbf, DTMF_SIGNAL_INTERCEPT_TONE },
  { 0x04, 0x04, 0xc0, DTMF_SIGNAL_MODEM_CALLING_TONE },
  { 0x04, 0x04, 0xc1, DTMF_SIGNAL_FAX_CALLING_TONE },
  { 0x04, 0x04, 0xc2, DTMF_SIGNAL_ANSWER_TONE },
  { 0x04, 0x04, 0xc3, DTMF_SIGNAL_REVERSED_ANSWER_TONE },
  { 0x04, 0x04, 0xc4, DTMF_SIGNAL_ANSAM_TONE },
  { 0x04, 0x04, 0xc5, DTMF_SIGNAL_REVERSED_ANSAM_TONE },
  { 0x04, 0x04, 0xc6, DTMF_SIGNAL_BELL103_ANSWER_TONE },
  { 0x04, 0x04, 0xc7, DTMF_SIGNAL_FAX_FLAGS },
  { 0x04, 0x04, 0xc8, DTMF_SIGNAL_G2_FAX_GROUP_ID },
  { 0x00, 0x04, 0xc9, DTMF_SIGNAL_HUMAN_SPEECH },
  { 0x04, 0x04, 0xca, DTMF_SIGNAL_ANSWERING_MACHINE_390 },
  { 0x02, 0x02, 0xf1, DTMF_MF_DIGIT_TONE_CODE_1 },
  { 0x02, 0x02, 0xf2, DTMF_MF_DIGIT_TONE_CODE_2 },
  { 0x02, 0x02, 0xf3, DTMF_MF_DIGIT_TONE_CODE_3 },
  { 0x02, 0x02, 0xf4, DTMF_MF_DIGIT_TONE_CODE_4 },
  { 0x02, 0x02, 0xf5, DTMF_MF_DIGIT_TONE_CODE_5 },
  { 0x02, 0x02, 0xf6, DTMF_MF_DIGIT_TONE_CODE_6 },
  { 0x02, 0x02, 0xf7, DTMF_MF_DIGIT_TONE_CODE_7 },
  { 0x02, 0x02, 0xf8, DTMF_MF_DIGIT_TONE_CODE_8 },
  { 0x02, 0x02, 0xf9, DTMF_MF_DIGIT_TONE_CODE_9 },
  { 0x02, 0x02, 0xfa, DTMF_MF_DIGIT_TONE_CODE_0 },
  { 0x02, 0x02, 0xfb, DTMF_MF_DIGIT_TONE_CODE_K1 },
  { 0x02, 0x02, 0xfc, DTMF_MF_DIGIT_TONE_CODE_K2 },
  { 0x02, 0x02, 0xfd, DTMF_MF_DIGIT_TONE_CODE_KP },
  { 0x02, 0x02, 0xfe, DTMF_MF_DIGIT_TONE_CODE_S1 },
  { 0x02, 0x02, 0xff, DTMF_MF_DIGIT_TONE_CODE_ST },

};

#define DTMF_DIGIT_MAP_ENTRIES ARRAY_SIZE(dtmf_digit_map)


static void dtmf_enable_receiver (PLCI   *plci, byte enable_mask)
{
  word min_digit_duration, min_gap_duration;

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_enable_receiver %02x",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, enable_mask));

  if (enable_mask != 0)
  {
    min_digit_duration = (plci->dtmf_rec_pulse_ms == 0) ? 40 : plci->dtmf_rec_pulse_ms;
    min_gap_duration = (plci->dtmf_rec_pause_ms == 0) ? 40 : plci->dtmf_rec_pause_ms;
    plci->internal_req_buffer[0] = DTMF_UDATA_REQUEST_ENABLE_RECEIVER;
    PUT_WORD (&plci->internal_req_buffer[1], min_digit_duration);
    PUT_WORD (&plci->internal_req_buffer[3], min_gap_duration);
    plci->NData[0].PLength = 5;

    PUT_WORD (&plci->internal_req_buffer[5], INTERNAL_IND_BUFFER_SIZE);
    plci->NData[0].PLength += 2;
    capidtmf_recv_enable (&(plci->capidtmf_state), min_digit_duration, min_gap_duration);

  }
  else
  {
    plci->internal_req_buffer[0] = DTMF_UDATA_REQUEST_DISABLE_RECEIVER;
    plci->NData[0].PLength = 1;

    capidtmf_recv_disable (&(plci->capidtmf_state));

  }
  plci->NData[0].P = plci->internal_req_buffer;
  plci->NL.X = plci->NData;
  plci->NL.ReqCh = 0;
  plci->NL.Req = plci->nl_req = (byte) N_UDATA;
  plci->adapter->request (&plci->NL);
}


static void dtmf_send_digits (PLCI   *plci, byte   *digit_buffer, word digit_count)
{
  word w, i;

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_send_digits %d",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, digit_count));

  plci->internal_req_buffer[0] = DTMF_UDATA_REQUEST_SEND_DIGITS;
  w = (plci->dtmf_send_pulse_ms == 0) ? 40 : plci->dtmf_send_pulse_ms;
  PUT_WORD (&plci->internal_req_buffer[1], w);
  w = (plci->dtmf_send_pause_ms == 0) ? 40 : plci->dtmf_send_pause_ms;
  PUT_WORD (&plci->internal_req_buffer[3], w);
  for (i = 0; i < digit_count; i++)
  {
    w = 0;
    while ((w < DTMF_DIGIT_MAP_ENTRIES)
      && (digit_buffer[i] != dtmf_digit_map[w].character))
    {
      w++;
    }
    plci->internal_req_buffer[5+i] = (w < DTMF_DIGIT_MAP_ENTRIES) ?
      dtmf_digit_map[w].code : DTMF_DIGIT_TONE_CODE_STAR;
  }
  plci->NData[0].PLength = 5 + digit_count;
  plci->NData[0].P = plci->internal_req_buffer;
  plci->NL.X = plci->NData;
  plci->NL.ReqCh = 0;
  plci->NL.Req = plci->nl_req = (byte) N_UDATA;
  plci->adapter->request (&plci->NL);
}


static void dtmf_rec_clear_config (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_rec_clear_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->dtmf_rec_active = 0;
  plci->dtmf_rec_pulse_ms = 0;
  plci->dtmf_rec_pause_ms = 0;

  capidtmf_init (&(plci->capidtmf_state), plci->adapter->u_law);

}


static void dtmf_send_clear_config (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_send_clear_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->dtmf_send_requests = 0;
  plci->dtmf_send_pulse_ms = 0;
  plci->dtmf_send_pause_ms = 0;
}


static void dtmf_prepare_switch (dword Id, PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_prepare_switch",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  while (plci->dtmf_send_requests != 0)
    dtmf_confirmation (Id, plci);
}


static word dtmf_save_config (dword Id, PLCI   *plci, byte Rc)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_save_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  return (GOOD);
}


static word dtmf_restore_config (dword Id, PLCI   *plci, byte Rc)
{
  word Info;

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_restore_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  Info = GOOD;
  if (plci->B1_facilities & B1_FACILITY_DTMFR)
  {
    switch (plci->adjust_b_state)
    {
    case ADJUST_B_RESTORE_DTMF_1:
      plci->internal_command = plci->adjust_b_command;
      if (plci_nl_busy (plci))
      {
        plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_1;
        break;
      }
      dtmf_enable_receiver (plci, plci->dtmf_rec_active);
      plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_2;
      break;
    case ADJUST_B_RESTORE_DTMF_2:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Reenable DTMF receiver failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _WRONG_STATE;
        break;
      }
      break;
    }
  }
  return (Info);
}


static void dtmf_command (dword Id, PLCI   *plci, byte Rc)
{
  word internal_command, Info;
  byte mask;
    byte result[4];

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_command %02x %04x %04x %d %d %d %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command,
    plci->dtmf_cmd, plci->dtmf_rec_pulse_ms, plci->dtmf_rec_pause_ms,
    plci->dtmf_send_pulse_ms, plci->dtmf_send_pause_ms));

  Info = GOOD;
  result[0] = 2;
  PUT_WORD (&result[1], DTMF_SUCCESS);
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  mask = 0x01;
  switch (plci->dtmf_cmd)
  {

  case DTMF_LISTEN_TONE_START:
    mask <<= 1;
  case DTMF_LISTEN_MF_START:
    mask <<= 1;

  case DTMF_LISTEN_START:
    switch (internal_command)
    {
    default:
      adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities |
        B1_FACILITY_DTMFR), DTMF_COMMAND_1);
    case DTMF_COMMAND_1:
      if (adjust_b_process (Id, plci, Rc) != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Load DTMF failed",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      if (plci->internal_command)
        return;
    case DTMF_COMMAND_2:
      if (plci_nl_busy (plci))
      {
        plci->internal_command = DTMF_COMMAND_2;
        return;
      }
      plci->internal_command = DTMF_COMMAND_3;
      dtmf_enable_receiver (plci, (byte)(plci->dtmf_rec_active | mask));
      return;
    case DTMF_COMMAND_3:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Enable DTMF receiver failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }

      plci->tone_last_indication_code = DTMF_SIGNAL_NO_TONE;

      plci->dtmf_rec_active |= mask;
      break;
    }
    break;


  case DTMF_LISTEN_TONE_STOP:
    mask <<= 1;
  case DTMF_LISTEN_MF_STOP:
    mask <<= 1;

  case DTMF_LISTEN_STOP:
    switch (internal_command)
    {
    default:
      plci->dtmf_rec_active &= ~mask;
      if (plci->dtmf_rec_active)
        break;
/*
    case DTMF_COMMAND_1:
      if (plci->dtmf_rec_active)
      {
        if (plci_nl_busy (plci))
        {
          plci->internal_command = DTMF_COMMAND_1;
          return;
        }
        plci->dtmf_rec_active &= ~mask;
        plci->internal_command = DTMF_COMMAND_2;
        dtmf_enable_receiver (plci, false);
        return;
      }
      Rc = OK;
    case DTMF_COMMAND_2:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Disable DTMF receiver failed %02x",
          UnMapId (Id), (char far *)(FILE_), __LINE__, Rc));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
*/
      adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities &
        ~(B1_FACILITY_DTMFX | B1_FACILITY_DTMFR)), DTMF_COMMAND_3);
    case DTMF_COMMAND_3:
      if (adjust_b_process (Id, plci, Rc) != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Unload DTMF failed",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      if (plci->internal_command)
        return;
      break;
    }
    break;


  case DTMF_SEND_TONE:
    mask <<= 1;
  case DTMF_SEND_MF:
    mask <<= 1;

  case DTMF_DIGITS_SEND:
    switch (internal_command)
    {
    default:
      adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities |
        ((plci->dtmf_parameter_length != 0) ? B1_FACILITY_DTMFX | B1_FACILITY_DTMFR : B1_FACILITY_DTMFX)),
        DTMF_COMMAND_1);
    case DTMF_COMMAND_1:
      if (adjust_b_process (Id, plci, Rc) != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Load DTMF failed",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      if (plci->internal_command)
        return;
    case DTMF_COMMAND_2:
      if (plci_nl_busy (plci))
      {
        plci->internal_command = DTMF_COMMAND_2;
        return;
      }
      plci->dtmf_msg_number_queue[(plci->dtmf_send_requests)++] = plci->number;
      plci->internal_command = DTMF_COMMAND_3;
      dtmf_send_digits (plci, &plci->saved_msg.parms[3].info[1], plci->saved_msg.parms[3].length);
      return;
    case DTMF_COMMAND_3:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Send DTMF digits failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        if (plci->dtmf_send_requests != 0)
          (plci->dtmf_send_requests)--;
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      return;
    }
    break;
  }
  sendf (plci->appl, _FACILITY_R | CONFIRM, Id & 0xffffL, plci->number,
    "wws", Info, SELECTOR_DTMF, result);
}


static byte dtmf_request (dword Id, word Number, DIVA_CAPI_ADAPTER   *a, PLCI   *plci, APPL   *appl, API_PARSE *msg)
{
  word Info;
  word i, j;
  byte mask;
    API_PARSE dtmf_parms[5];
    byte result[40];

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_request",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  Info = GOOD;
  result[0] = 2;
  PUT_WORD (&result[1], DTMF_SUCCESS);
  if (!(a->profile.Global_Options & GL_DTMF_SUPPORTED))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Facility not supported",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    Info = _FACILITY_NOT_SUPPORTED;
  }
  else if (api_parse (&msg[1].info[1], msg[1].length, "w", dtmf_parms))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    Info = _WRONG_MESSAGE_FORMAT;
  }

  else if ((GET_WORD (dtmf_parms[0].info) == DTMF_GET_SUPPORTED_DETECT_CODES)
    || (GET_WORD (dtmf_parms[0].info) == DTMF_GET_SUPPORTED_SEND_CODES))
  {
    if (!((a->requested_options_table[appl->Id-1])
        & (1L << PRIVATE_DTMF_TONE)))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: DTMF unknown request %04x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, GET_WORD (dtmf_parms[0].info)));
      PUT_WORD (&result[1], DTMF_UNKNOWN_REQUEST);
    }
    else
    {
      for (i = 0; i < 32; i++)
        result[4 + i] = 0;
      if (GET_WORD (dtmf_parms[0].info) == DTMF_GET_SUPPORTED_DETECT_CODES)
      {
        for (i = 0; i < DTMF_DIGIT_MAP_ENTRIES; i++)
        {
          if (dtmf_digit_map[i].listen_mask != 0)
            result[4 + (dtmf_digit_map[i].character >> 3)] |= (1 << (dtmf_digit_map[i].character & 0x7));
        }
      }
      else
      {
        for (i = 0; i < DTMF_DIGIT_MAP_ENTRIES; i++)
        {
          if (dtmf_digit_map[i].send_mask != 0)
            result[4 + (dtmf_digit_map[i].character >> 3)] |= (1 << (dtmf_digit_map[i].character & 0x7));
        }
      }
      result[0] = 3 + 32;
      result[3] = 32;
    }
  }

  else if (plci == NULL)
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Wrong PLCI",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    Info = _WRONG_IDENTIFIER;
  }
  else
  {
    if (!plci->State
     || !plci->NL.Id || plci->nl_remove_id)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Wrong state",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
      Info = _WRONG_STATE;
    }
    else
    {
      plci->command = 0;
      plci->dtmf_cmd = GET_WORD (dtmf_parms[0].info);
      mask = 0x01;
      switch (plci->dtmf_cmd)
      {

      case DTMF_LISTEN_TONE_START:
      case DTMF_LISTEN_TONE_STOP:
        mask <<= 1;
      case DTMF_LISTEN_MF_START:
      case DTMF_LISTEN_MF_STOP:
        mask <<= 1;
        if (!((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[appl->Id-1])
          & (1L << PRIVATE_DTMF_TONE)))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: DTMF unknown request %04x",
            UnMapId (Id), (char   *)(FILE_), __LINE__, GET_WORD (dtmf_parms[0].info)));
          PUT_WORD (&result[1], DTMF_UNKNOWN_REQUEST);
          break;
        }

      case DTMF_LISTEN_START:
      case DTMF_LISTEN_STOP:
        if (!(a->manufacturer_features & MANUFACTURER_FEATURE_HARDDTMF)
         && !(a->manufacturer_features & MANUFACTURER_FEATURE_SOFTDTMF_RECEIVE))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Facility not supported",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _FACILITY_NOT_SUPPORTED;
          break;
        }
        if (mask & DTMF_LISTEN_ACTIVE_FLAG)
        {
          if (api_parse (&msg[1].info[1], msg[1].length, "wwws", dtmf_parms))
          {
            plci->dtmf_rec_pulse_ms = 0;
            plci->dtmf_rec_pause_ms = 0;
          }
          else
          {
            plci->dtmf_rec_pulse_ms = GET_WORD (dtmf_parms[1].info);
            plci->dtmf_rec_pause_ms = GET_WORD (dtmf_parms[2].info);
          }
        }
        start_internal_command (Id, plci, dtmf_command);
        return (false);


      case DTMF_SEND_TONE:
        mask <<= 1;
      case DTMF_SEND_MF:
        mask <<= 1;
        if (!((plci->requested_options_conn | plci->requested_options | plci->adapter->requested_options_table[appl->Id-1])
          & (1L << PRIVATE_DTMF_TONE)))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: DTMF unknown request %04x",
            UnMapId (Id), (char   *)(FILE_), __LINE__, GET_WORD (dtmf_parms[0].info)));
          PUT_WORD (&result[1], DTMF_UNKNOWN_REQUEST);
          break;
        }

      case DTMF_DIGITS_SEND:
        if (api_parse (&msg[1].info[1], msg[1].length, "wwws", dtmf_parms))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_MESSAGE_FORMAT;
          break;
        }
        if (mask & DTMF_LISTEN_ACTIVE_FLAG)
        {
          plci->dtmf_send_pulse_ms = GET_WORD (dtmf_parms[1].info);
          plci->dtmf_send_pause_ms = GET_WORD (dtmf_parms[2].info);
        }
        i = 0;
        j = 0;
        while ((i < dtmf_parms[3].length) && (j < DTMF_DIGIT_MAP_ENTRIES))
        {
          j = 0;
          while ((j < DTMF_DIGIT_MAP_ENTRIES)
            && ((dtmf_parms[3].info[i+1] != dtmf_digit_map[j].character)
             || ((dtmf_digit_map[j].send_mask & mask) == 0)))
          {
            j++;
          }
          i++;
        }
        if (j == DTMF_DIGIT_MAP_ENTRIES)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Incorrect DTMF digit %02x",
            UnMapId (Id), (char   *)(FILE_), __LINE__, dtmf_parms[3].info[i]));
          PUT_WORD (&result[1], DTMF_INCORRECT_DIGIT);
          break;
        }
        if (plci->dtmf_send_requests >= ARRAY_SIZE(plci->dtmf_msg_number_queue))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: DTMF request overrun",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_STATE;
          break;
        }
        api_save_msg (dtmf_parms, "wwws", &plci->saved_msg);
        start_internal_command (Id, plci, dtmf_command);
        return (false);

      default:
        dbug (1, dprintf ("[%06lx] %s,%d: DTMF unknown request %04x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, plci->dtmf_cmd));
        PUT_WORD (&result[1], DTMF_UNKNOWN_REQUEST);
      }
    }
  }
  sendf (appl, _FACILITY_R | CONFIRM, Id & 0xffffL, Number,
    "wws", Info, SELECTOR_DTMF, result);
  return (false);
}


static void dtmf_confirmation (dword Id, PLCI   *plci)
{
  word Info;
  word i;
    byte result[4];

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_confirmation",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  Info = GOOD;
  result[0] = 2;
  PUT_WORD (&result[1], DTMF_SUCCESS);
  if (plci->dtmf_send_requests != 0)
  {
    sendf (plci->appl, _FACILITY_R | CONFIRM, Id & 0xffffL, plci->dtmf_msg_number_queue[0],
      "wws", GOOD, SELECTOR_DTMF, result);
    (plci->dtmf_send_requests)--;
    for (i = 0; i < plci->dtmf_send_requests; i++)
      plci->dtmf_msg_number_queue[i] = plci->dtmf_msg_number_queue[i+1];      
  }
}


static void dtmf_indication (dword Id, PLCI   *plci, byte   *msg, word length)
{
  word i, j, n;

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_indication",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  n = 0;
  for (i = 1; i < length; i++)
  {
    j = 0;
    while ((j < DTMF_DIGIT_MAP_ENTRIES)
      && ((msg[i] != dtmf_digit_map[j].code)
       || ((dtmf_digit_map[j].listen_mask & plci->dtmf_rec_active) == 0)))
    {
      j++;
    }
    if (j < DTMF_DIGIT_MAP_ENTRIES)
    {

      if ((dtmf_digit_map[j].listen_mask & DTMF_TONE_LISTEN_ACTIVE_FLAG)
       && (plci->tone_last_indication_code == DTMF_SIGNAL_NO_TONE)
       && (dtmf_digit_map[j].character != DTMF_SIGNAL_UNIDENTIFIED_TONE))
      {
        if (n + 1 == i)
        {
          for (i = length; i > n + 1; i--)
            msg[i] = msg[i - 1];
          length++;
          i++;
        }
        msg[++n] = DTMF_SIGNAL_UNIDENTIFIED_TONE;
      }
      plci->tone_last_indication_code = dtmf_digit_map[j].character;

      msg[++n] = dtmf_digit_map[j].character;
    }
  }
  if (n != 0)
  {
    msg[0] = (byte) n;
    sendf (plci->appl, _FACILITY_I, Id & 0xffffL, 0, "wS", SELECTOR_DTMF, msg);
  }
}


/*------------------------------------------------------------------*/
/* DTMF parameters                                                  */
/*------------------------------------------------------------------*/

static void dtmf_parameter_write (PLCI   *plci)
{
  word i;
    byte parameter_buffer[DTMF_PARAMETER_BUFFER_SIZE + 2];

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_parameter_write",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  parameter_buffer[0] = plci->dtmf_parameter_length + 1;
  parameter_buffer[1] = DSP_CTRL_SET_DTMF_PARAMETERS;
  for (i = 0; i < plci->dtmf_parameter_length; i++)
    parameter_buffer[2+i] = plci->dtmf_parameter_buffer[i];
  add_p (plci, FTY, parameter_buffer);
  sig_req (plci, TEL_CTRL, 0);
  send_req (plci);
}


static void dtmf_parameter_clear_config (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_parameter_clear_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->dtmf_parameter_length = 0;
}


static void dtmf_parameter_prepare_switch (dword Id, PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_parameter_prepare_switch",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

}


static word dtmf_parameter_save_config (dword Id, PLCI   *plci, byte Rc)
{

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_parameter_save_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  return (GOOD);
}


static word dtmf_parameter_restore_config (dword Id, PLCI   *plci, byte Rc)
{
  word Info;

  dbug (1, dprintf ("[%06lx] %s,%d: dtmf_parameter_restore_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  Info = GOOD;
  if ((plci->B1_facilities & B1_FACILITY_DTMFR)
   && (plci->dtmf_parameter_length != 0))
  {
    switch (plci->adjust_b_state)
    {
    case ADJUST_B_RESTORE_DTMF_PARAMETER_1:
      plci->internal_command = plci->adjust_b_command;
      if (plci->sig_req)
      {
        plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_PARAMETER_1;
        break;
      }
      dtmf_parameter_write (plci);
      plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_PARAMETER_2;
      break;
    case ADJUST_B_RESTORE_DTMF_PARAMETER_2:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Restore DTMF parameters failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _WRONG_STATE;
        break;
      }
      break;
    }
  }
  return (Info);
}


/*------------------------------------------------------------------*/
/* Line interconnect facilities                                     */
/*------------------------------------------------------------------*/


LI_CONFIG   *li_config_table;
word li_total_channels;


/*------------------------------------------------------------------*/
/* translate a CHI information element to a channel number          */
/* returns 0xff - any channel                                       */
/*         0xfe - chi wrong coding                                  */
/*         0xfd - D-channel                                         */
/*         0x00 - no channel                                        */
/*         else channel number / PRI: timeslot                      */
/* if channels is provided we accept more than one channel.         */
/*------------------------------------------------------------------*/

static byte chi_to_channel (byte   *chi, dword *pchannelmap)
{
  int p;
  int i;
  dword map;
  byte excl;
  byte ofs;
  byte ch;

  if (pchannelmap) *pchannelmap = 0;
  if(!chi[0]) return 0xff;
  excl = 0;

  if(chi[1] & 0x20) {
    if(chi[0]==1 && chi[1]==0xac) return 0xfd; /* exclusive d-channel */
    for(i=1; i<chi[0] && !(chi[i] &0x80); i++);
    if(i==chi[0] || !(chi[i] &0x80)) return 0xfe;
    if((chi[1] |0xc8)!=0xe9) return 0xfe;
    if(chi[1] &0x08) excl = 0x40;

        /* int. id present */
    if(chi[1] &0x40) {
      p=i+1;
      for(i=p; i<chi[0] && !(chi[i] &0x80); i++);
      if(i==chi[0] || !(chi[i] &0x80)) return 0xfe;
    }

        /* coding standard, Number/Map, Channel Type */
    p=i+1;
    for(i=p; i<chi[0] && !(chi[i] &0x80); i++);
    if(i==chi[0] || !(chi[i] &0x80)) return 0xfe;
    if((chi[p]|0xd0)!=0xd3) return 0xfe;

        /* Number/Map */
    if(chi[p] &0x10) {

        /* map */
      if((chi[0]-p)==4) ofs = 0;
      else if((chi[0]-p)==3) ofs = 1;
      else return 0xfe;
      ch = 0;
      map = 0;
      for(i=0; i<4 && p<chi[0]; i++) {
        p++;
        ch += 8;
        map <<= 8;
        if(chi[p]) {
          for (ch=0; !(chi[p] & (1 << ch)); ch++);
          map |= chi[p];
        }
      }
      ch += ofs;
      map <<= ofs;
    }
    else {

        /* number */
      p=i+1;
      ch = chi[p] &0x3f;
      if(pchannelmap) {
        if((byte)(chi[0]-p)>30) return 0xfe;
        map = 0;
        for(i=p; i<=chi[0]; i++) {
          if ((chi[i] &0x7f) > 31) return 0xfe;
          map |= (1L << (chi[i] &0x7f));
        }
      }
      else {
        if(p!=chi[0]) return 0xfe;
        if (ch > 31) return 0xfe;
        map = (1L << ch);
      }
      if(chi[p] &0x40) return 0xfe;
    }
    if (pchannelmap) *pchannelmap = map;
    else if (map != ((dword)(1L << ch))) return 0xfe;
    return (byte)(excl | ch);
  }
  else {  /* not PRI */
    for(i=1; i<chi[0] && !(chi[i] &0x80); i++);
    if(i!=chi[0] || !(chi[i] &0x80)) return 0xfe;
    if(chi[1] &0x08) excl = 0x40;

    switch(chi[1] |0x98) {
    case 0x98: return 0;
    case 0x99:
      if (pchannelmap) *pchannelmap = 2;
      return excl |1;
    case 0x9a:
      if (pchannelmap) *pchannelmap = 4;
      return excl |2;
    case 0x9b: return 0xff;
    case 0x9c: return 0xfd; /* d-ch */
    default: return 0xfe;
    }
  }
}


static void mixer_set_bchannel_id_esc (PLCI   *plci, byte bchannel_id)
{
  DIVA_CAPI_ADAPTER   *a;
  PLCI   *splci;
  byte old_id;

  a = plci->adapter;
  old_id = plci->li_bchannel_id;
  if (a->li_pri)
  {
    if ((old_id != 0) && (li_config_table[a->li_base + (old_id - 1)].plci == plci))
      li_config_table[a->li_base + (old_id - 1)].plci = NULL;
    plci->li_bchannel_id = (bchannel_id & 0x1f) + 1;
    if (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == NULL)
      li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci = plci;
  }
  else
  {
    if (((bchannel_id & 0x03) == 1) || ((bchannel_id & 0x03) == 2))
    {
      if ((old_id != 0) && (li_config_table[a->li_base + (old_id - 1)].plci == plci))
        li_config_table[a->li_base + (old_id - 1)].plci = NULL;
      plci->li_bchannel_id = bchannel_id & 0x03;
      if ((a->AdvSignalPLCI != NULL) && (a->AdvSignalPLCI != plci) && (a->AdvSignalPLCI->tel == ADV_VOICE))
      {
        splci = a->AdvSignalPLCI;
        if (li_config_table[a->li_base + (2 - plci->li_bchannel_id)].plci == NULL)
        {
          if ((splci->li_bchannel_id != 0)
           && (li_config_table[a->li_base + (splci->li_bchannel_id - 1)].plci == splci))
          {
            li_config_table[a->li_base + (splci->li_bchannel_id - 1)].plci = NULL;
          }
          splci->li_bchannel_id = 3 - plci->li_bchannel_id;
          li_config_table[a->li_base + (2 - plci->li_bchannel_id)].plci = splci;
          dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_set_bchannel_id_esc %d",
            (dword)((splci->Id << 8) | UnMapController (splci->adapter->Id)),
            (char   *)(FILE_), __LINE__, splci->li_bchannel_id));
        }
      }
      if (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == NULL)
        li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci = plci;
    }
  }
  if ((old_id == 0) && (plci->li_bchannel_id != 0)
   && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    mixer_clear_config (plci);
  }
  dbug (1, dprintf ("[%06lx] %s,%d: mixer_set_bchannel_id_esc %d %d",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, bchannel_id, plci->li_bchannel_id));
}


static void mixer_set_bchannel_id (PLCI   *plci, byte   *chi)
{
  DIVA_CAPI_ADAPTER   *a;
  PLCI   *splci;
  byte ch, old_id;

  a = plci->adapter;
  old_id = plci->li_bchannel_id;
  ch = chi_to_channel (chi, NULL);
  if (!(ch & 0x80))
  {
    if (a->li_pri)
    {
      if ((old_id != 0) && (li_config_table[a->li_base + (old_id - 1)].plci == plci))
        li_config_table[a->li_base + (old_id - 1)].plci = NULL;
      plci->li_bchannel_id = (ch & 0x1f) + 1;
      if (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == NULL)
        li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci = plci;
    }
    else
    {
      if (((ch & 0x1f) == 1) || ((ch & 0x1f) == 2))
      {
        if ((old_id != 0) && (li_config_table[a->li_base + (old_id - 1)].plci == plci))
          li_config_table[a->li_base + (old_id - 1)].plci = NULL;
        plci->li_bchannel_id = ch & 0x1f;
        if ((a->AdvSignalPLCI != NULL) && (a->AdvSignalPLCI != plci) && (a->AdvSignalPLCI->tel == ADV_VOICE))
        {
          splci = a->AdvSignalPLCI;
          if (li_config_table[a->li_base + (2 - plci->li_bchannel_id)].plci == NULL)
          {
            if ((splci->li_bchannel_id != 0)
             && (li_config_table[a->li_base + (splci->li_bchannel_id - 1)].plci == splci))
            {
              li_config_table[a->li_base + (splci->li_bchannel_id - 1)].plci = NULL;
            }
            splci->li_bchannel_id = 3 - plci->li_bchannel_id;
            li_config_table[a->li_base + (2 - plci->li_bchannel_id)].plci = splci;
            dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_set_bchannel_id %d",
              (dword)((splci->Id << 8) | UnMapController (splci->adapter->Id)),
              (char   *)(FILE_), __LINE__, splci->li_bchannel_id));
          }
        }
        if (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == NULL)
          li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci = plci;
      }
    }
  }
  if ((old_id == 0) && (plci->li_bchannel_id != 0)
   && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    mixer_clear_config (plci);
  }
  dbug (1, dprintf ("[%06lx] %s,%d: mixer_set_bchannel_id %02x %d",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, ch, plci->li_bchannel_id));
}


#define MIXER_MAX_DUMP_CHANNELS 34

static void mixer_calculate_coefs (DIVA_CAPI_ADAPTER   *a)
{
static char hex_digit_table[0x10] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
  word n, i, j;
  char *p;
    char hex_line[2 * MIXER_MAX_DUMP_CHANNELS + MIXER_MAX_DUMP_CHANNELS / 8 + 4];

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_calculate_coefs",
    (dword)(UnMapController (a->Id)), (char   *)(FILE_), __LINE__));

  for (i = 0; i < li_total_channels; i++)
  {
    li_config_table[i].channel &= LI_CHANNEL_ADDRESSES_SET;
    if (li_config_table[i].chflags != 0)
      li_config_table[i].channel |= LI_CHANNEL_INVOLVED;
    else
    {
      for (j = 0; j < li_total_channels; j++)
      {
        if (((li_config_table[i].flag_table[j]) != 0)
         || ((li_config_table[j].flag_table[i]) != 0))
        {
          li_config_table[i].channel |= LI_CHANNEL_INVOLVED;
        }
        if (((li_config_table[i].flag_table[j] & LI_FLAG_CONFERENCE) != 0)
         || ((li_config_table[j].flag_table[i] & LI_FLAG_CONFERENCE) != 0))
        {
          li_config_table[i].channel |= LI_CHANNEL_CONFERENCE;
        }
      }
    }
  }
  for (i = 0; i < li_total_channels; i++)
  {
    for (j = 0; j < li_total_channels; j++)
    {
      li_config_table[i].coef_table[j] &= ~(LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC);
      if (li_config_table[i].flag_table[j] & LI_FLAG_CONFERENCE)
        li_config_table[i].coef_table[j] |= LI_COEF_CH_CH;
    }
  }
  for (n = 0; n < li_total_channels; n++)
  {
    if (li_config_table[n].channel & LI_CHANNEL_CONFERENCE)
    {
      for (i = 0; i < li_total_channels; i++)
      {
        if (li_config_table[i].channel & LI_CHANNEL_CONFERENCE)
        {
          for (j = 0; j < li_total_channels; j++)
          {
            li_config_table[i].coef_table[j] |=
              li_config_table[i].coef_table[n] & li_config_table[n].coef_table[j];
          }
        }
      }
    }
  }
  for (i = 0; i < li_total_channels; i++)
  {
    if (li_config_table[i].channel & LI_CHANNEL_INVOLVED)
    {
      li_config_table[i].coef_table[i] &= ~LI_COEF_CH_CH;
      for (j = 0; j < li_total_channels; j++)
      {
        if (li_config_table[i].coef_table[j] & LI_COEF_CH_CH)
          li_config_table[i].flag_table[j] |= LI_FLAG_CONFERENCE;
      }
      if (li_config_table[i].flag_table[i] & LI_FLAG_CONFERENCE)
        li_config_table[i].coef_table[i] |= LI_COEF_CH_CH;
    }
  }
  for (i = 0; i < li_total_channels; i++)
  {
    if (li_config_table[i].channel & LI_CHANNEL_INVOLVED)
    {
      for (j = 0; j < li_total_channels; j++)
      {
        if (li_config_table[i].flag_table[j] & LI_FLAG_INTERCONNECT)
          li_config_table[i].coef_table[j] |= LI_COEF_CH_CH;
        if (li_config_table[i].flag_table[j] & LI_FLAG_MONITOR)
          li_config_table[i].coef_table[j] |= LI_COEF_CH_PC;
        if (li_config_table[i].flag_table[j] & LI_FLAG_MIX)
          li_config_table[i].coef_table[j] |= LI_COEF_PC_CH;
        if (li_config_table[i].flag_table[j] & LI_FLAG_PCCONNECT)
          li_config_table[i].coef_table[j] |= LI_COEF_PC_PC;
      }
      if (li_config_table[i].chflags & LI_CHFLAG_MONITOR)
      {
        for (j = 0; j < li_total_channels; j++)
        {
          if (li_config_table[i].flag_table[j] & LI_FLAG_INTERCONNECT)
          {
            li_config_table[i].coef_table[j] |= LI_COEF_CH_PC;
            if (li_config_table[j].chflags & LI_CHFLAG_MIX)
              li_config_table[i].coef_table[j] |= LI_COEF_PC_CH | LI_COEF_PC_PC;
          }
        }
      }
      if (li_config_table[i].chflags & LI_CHFLAG_MIX)
      {
        for (j = 0; j < li_total_channels; j++)
        {
          if (li_config_table[j].flag_table[i] & LI_FLAG_INTERCONNECT)
            li_config_table[j].coef_table[i] |= LI_COEF_PC_CH;
        }
      }
      if (li_config_table[i].chflags & LI_CHFLAG_LOOP)
      {
        for (j = 0; j < li_total_channels; j++)
        {
          if (li_config_table[i].flag_table[j] & LI_FLAG_INTERCONNECT)
          {
            for (n = 0; n < li_total_channels; n++)
            {
              if (li_config_table[n].flag_table[i] & LI_FLAG_INTERCONNECT)
              {
                li_config_table[n].coef_table[j] |= LI_COEF_CH_CH;
                if (li_config_table[j].chflags & LI_CHFLAG_MIX)
                {
                  li_config_table[n].coef_table[j] |= LI_COEF_PC_CH;
                  if (li_config_table[n].chflags & LI_CHFLAG_MONITOR)
                    li_config_table[n].coef_table[j] |= LI_COEF_CH_PC | LI_COEF_PC_PC;
                }
                else if (li_config_table[n].chflags & LI_CHFLAG_MONITOR)
                  li_config_table[n].coef_table[j] |= LI_COEF_CH_PC;
              }
            }
          }
        }
      }
    }
  }
  for (i = 0; i < li_total_channels; i++)
  {
    if (li_config_table[i].channel & LI_CHANNEL_INVOLVED)
    {
      if (li_config_table[i].chflags & (LI_CHFLAG_MONITOR | LI_CHFLAG_MIX | LI_CHFLAG_LOOP))
        li_config_table[i].channel |= LI_CHANNEL_ACTIVE;
      if (li_config_table[i].chflags & LI_CHFLAG_MONITOR)
        li_config_table[i].channel |= LI_CHANNEL_RX_DATA;
      if (li_config_table[i].chflags & LI_CHFLAG_MIX)
        li_config_table[i].channel |= LI_CHANNEL_TX_DATA;
      for (j = 0; j < li_total_channels; j++)
      {
        if ((li_config_table[i].flag_table[j] &
          (LI_FLAG_INTERCONNECT | LI_FLAG_PCCONNECT | LI_FLAG_CONFERENCE | LI_FLAG_MONITOR))
         || (li_config_table[j].flag_table[i] &
          (LI_FLAG_INTERCONNECT | LI_FLAG_PCCONNECT | LI_FLAG_CONFERENCE | LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX)))
        {
          li_config_table[i].channel |= LI_CHANNEL_ACTIVE;
        }
        if (li_config_table[i].flag_table[j] & (LI_FLAG_PCCONNECT | LI_FLAG_MONITOR))
          li_config_table[i].channel |= LI_CHANNEL_RX_DATA;
        if (li_config_table[j].flag_table[i] & (LI_FLAG_PCCONNECT | LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX))
          li_config_table[i].channel |= LI_CHANNEL_TX_DATA;
      }
      if (!(li_config_table[i].channel & LI_CHANNEL_ACTIVE))
      {
        li_config_table[i].coef_table[i] |= LI_COEF_PC_CH | LI_COEF_CH_PC;
        li_config_table[i].channel |= LI_CHANNEL_TX_DATA | LI_CHANNEL_RX_DATA;
      }
    }
  }
  for (i = 0; i < li_total_channels; i++)
  {
    if (li_config_table[i].channel & LI_CHANNEL_INVOLVED)
    {
      j = 0;
      while ((j < li_total_channels) && !(li_config_table[i].flag_table[j] & LI_FLAG_ANNOUNCEMENT))
        j++;
      if (j < li_total_channels)
      {
        for (j = 0; j < li_total_channels; j++)
        {
          li_config_table[i].coef_table[j] &= ~(LI_COEF_CH_CH | LI_COEF_PC_CH);
          if (li_config_table[i].flag_table[j] & LI_FLAG_ANNOUNCEMENT)
            li_config_table[i].coef_table[j] |= LI_COEF_PC_CH;
        }
      }
    }
  }
  n = li_total_channels;
  if (n > MIXER_MAX_DUMP_CHANNELS)
    n = MIXER_MAX_DUMP_CHANNELS;
  p = hex_line;
  for (j = 0; j < n; j++)
  {
    if ((j & 0x7) == 0)
      *(p++) = ' ';
    *(p++) = hex_digit_table[li_config_table[j].curchnl >> 4];
    *(p++) = hex_digit_table[li_config_table[j].curchnl & 0xf];
  }
  *p = '\0';
  dbug (1, dprintf ("[%06lx] CURRENT %s",
    (dword)(UnMapController (a->Id)), (char   *) hex_line));
  p = hex_line;
  for (j = 0; j < n; j++)
  {
    if ((j & 0x7) == 0)
      *(p++) = ' ';
    *(p++) = hex_digit_table[li_config_table[j].channel >> 4];
    *(p++) = hex_digit_table[li_config_table[j].channel & 0xf];
  }
  *p = '\0';
  dbug (1, dprintf ("[%06lx] CHANNEL %s",
    (dword)(UnMapController (a->Id)), (char   *) hex_line));
  p = hex_line;
  for (j = 0; j < n; j++)
  {
    if ((j & 0x7) == 0)
      *(p++) = ' ';
    *(p++) = hex_digit_table[li_config_table[j].chflags >> 4];
    *(p++) = hex_digit_table[li_config_table[j].chflags & 0xf];
  }
  *p = '\0';
  dbug (1, dprintf ("[%06lx] CHFLAG  %s",
    (dword)(UnMapController (a->Id)), (char   *) hex_line));
  for (i = 0; i < n; i++)
  {
    p = hex_line;
    for (j = 0; j < n; j++)
    {
      if ((j & 0x7) == 0)
        *(p++) = ' ';
      *(p++) = hex_digit_table[li_config_table[i].flag_table[j] >> 4];
      *(p++) = hex_digit_table[li_config_table[i].flag_table[j] & 0xf];
    }
    *p = '\0';
    dbug (1, dprintf ("[%06lx] FLAG[%02x]%s",
      (dword)(UnMapController (a->Id)), i, (char   *) hex_line));
  }
  for (i = 0; i < n; i++)
  {
    p = hex_line;
    for (j = 0; j < n; j++)
    {
      if ((j & 0x7) == 0)
        *(p++) = ' ';
      *(p++) = hex_digit_table[li_config_table[i].coef_table[j] >> 4];
      *(p++) = hex_digit_table[li_config_table[i].coef_table[j] & 0xf];
    }
    *p = '\0';
    dbug (1, dprintf ("[%06lx] COEF[%02x]%s",
      (dword)(UnMapController (a->Id)), i, (char   *) hex_line));
  }
}


static struct
{
  byte mask;
  byte line_flags;
} mixer_write_prog_pri[] =
{
  { LI_COEF_CH_CH, 0 },
  { LI_COEF_CH_PC, MIXER_COEF_LINE_TO_PC_FLAG },
  { LI_COEF_PC_CH, MIXER_COEF_LINE_FROM_PC_FLAG },
  { LI_COEF_PC_PC, MIXER_COEF_LINE_TO_PC_FLAG | MIXER_COEF_LINE_FROM_PC_FLAG }
};

static struct
{
  byte from_ch;
  byte to_ch;
  byte mask;
  byte xconnect_override;
} mixer_write_prog_bri[] =
{
  { 0, 0, LI_COEF_CH_CH, 0x01 },  /* B      to B      */
  { 1, 0, LI_COEF_CH_CH, 0x01 },  /* Alt B  to B      */
  { 0, 0, LI_COEF_PC_CH, 0x80 },  /* PC     to B      */
  { 1, 0, LI_COEF_PC_CH, 0x01 },  /* Alt PC to B      */
  { 2, 0, LI_COEF_CH_CH, 0x00 },  /* IC     to B      */
  { 3, 0, LI_COEF_CH_CH, 0x00 },  /* Alt IC to B      */
  { 0, 0, LI_COEF_CH_PC, 0x80 },  /* B      to PC     */
  { 1, 0, LI_COEF_CH_PC, 0x01 },  /* Alt B  to PC     */
  { 0, 0, LI_COEF_PC_PC, 0x01 },  /* PC     to PC     */
  { 1, 0, LI_COEF_PC_PC, 0x01 },  /* Alt PC to PC     */
  { 2, 0, LI_COEF_CH_PC, 0x00 },  /* IC     to PC     */
  { 3, 0, LI_COEF_CH_PC, 0x00 },  /* Alt IC to PC     */
  { 0, 2, LI_COEF_CH_CH, 0x00 },  /* B      to IC     */
  { 1, 2, LI_COEF_CH_CH, 0x00 },  /* Alt B  to IC     */
  { 0, 2, LI_COEF_PC_CH, 0x00 },  /* PC     to IC     */
  { 1, 2, LI_COEF_PC_CH, 0x00 },  /* Alt PC to IC     */
  { 2, 2, LI_COEF_CH_CH, 0x00 },  /* IC     to IC     */
  { 3, 2, LI_COEF_CH_CH, 0x00 },  /* Alt IC to IC     */
  { 1, 1, LI_COEF_CH_CH, 0x01 },  /* Alt B  to Alt B  */
  { 0, 1, LI_COEF_CH_CH, 0x01 },  /* B      to Alt B  */
  { 1, 1, LI_COEF_PC_CH, 0x80 },  /* Alt PC to Alt B  */
  { 0, 1, LI_COEF_PC_CH, 0x01 },  /* PC     to Alt B  */
  { 3, 1, LI_COEF_CH_CH, 0x00 },  /* Alt IC to Alt B  */
  { 2, 1, LI_COEF_CH_CH, 0x00 },  /* IC     to Alt B  */
  { 1, 1, LI_COEF_CH_PC, 0x80 },  /* Alt B  to Alt PC */
  { 0, 1, LI_COEF_CH_PC, 0x01 },  /* B      to Alt PC */
  { 1, 1, LI_COEF_PC_PC, 0x01 },  /* Alt PC to Alt PC */
  { 0, 1, LI_COEF_PC_PC, 0x01 },  /* PC     to Alt PC */
  { 3, 1, LI_COEF_CH_PC, 0x00 },  /* Alt IC to Alt PC */
  { 2, 1, LI_COEF_CH_PC, 0x00 },  /* IC     to Alt PC */
  { 1, 3, LI_COEF_CH_CH, 0x00 },  /* Alt B  to Alt IC */
  { 0, 3, LI_COEF_CH_CH, 0x00 },  /* B      to Alt IC */
  { 1, 3, LI_COEF_PC_CH, 0x00 },  /* Alt PC to Alt IC */
  { 0, 3, LI_COEF_PC_CH, 0x00 },  /* PC     to Alt IC */
  { 3, 3, LI_COEF_CH_CH, 0x00 },  /* Alt IC to Alt IC */
  { 2, 3, LI_COEF_CH_CH, 0x00 }   /* IC     to Alt IC */
};

static byte mixer_swapped_index_bri[] =
{
  18,  /* B      to B      */
  19,  /* Alt B  to B      */
  20,  /* PC     to B      */
  21,  /* Alt PC to B      */
  22,  /* IC     to B      */
  23,  /* Alt IC to B      */
  24,  /* B      to PC     */
  25,  /* Alt B  to PC     */
  26,  /* PC     to PC     */
  27,  /* Alt PC to PC     */
  28,  /* IC     to PC     */
  29,  /* Alt IC to PC     */
  30,  /* B      to IC     */
  31,  /* Alt B  to IC     */
  32,  /* PC     to IC     */
  33,  /* Alt PC to IC     */
  34,  /* IC     to IC     */
  35,  /* Alt IC to IC     */
  0,   /* Alt B  to Alt B  */
  1,   /* B      to Alt B  */
  2,   /* Alt PC to Alt B  */
  3,   /* PC     to Alt B  */
  4,   /* Alt IC to Alt B  */
  5,   /* IC     to Alt B  */
  6,   /* Alt B  to Alt PC */
  7,   /* B      to Alt PC */
  8,   /* Alt PC to Alt PC */
  9,   /* PC     to Alt PC */
  10,  /* Alt IC to Alt PC */
  11,  /* IC     to Alt PC */
  12,  /* Alt B  to Alt IC */
  13,  /* B      to Alt IC */
  14,  /* Alt PC to Alt IC */
  15,  /* PC     to Alt IC */
  16,  /* Alt IC to Alt IC */
  17   /* IC     to Alt IC */
};

static struct
{
  byte mask;
  byte from_pc;
  byte to_pc;
} xconnect_write_prog[] =
{
  { LI_COEF_CH_CH, false, false },
  { LI_COEF_CH_PC, false, true },
  { LI_COEF_PC_CH, true, false },
  { LI_COEF_PC_PC, true, true }
};


static void xconnect_query_addresses (PLCI   *plci)
{
  DIVA_CAPI_ADAPTER   *a;
  word w, ch;
  byte   *p;

  dbug (1, dprintf ("[%06lx] %s,%d: xconnect_query_addresses",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  a = plci->adapter;
  if (a->li_pri && ((plci->li_bchannel_id == 0)
   || (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci != plci)))
  {
    dbug (1, dprintf ("[%06x] %s,%d: Channel id wiped out",
      (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
      (char   *)(FILE_), __LINE__));
    return;
  }
  p = plci->internal_req_buffer;
  ch = (a->li_pri) ? plci->li_bchannel_id - 1 : 0;
  *(p++) = UDATA_REQUEST_XCONNECT_FROM;
  w = ch;
  *(p++) = (byte) w;
  *(p++) = (byte)(w >> 8);
  w = ch | XCONNECT_CHANNEL_PORT_PC;
  *(p++) = (byte) w;
  *(p++) = (byte)(w >> 8);
  plci->NData[0].P = plci->internal_req_buffer;
  plci->NData[0].PLength = p - plci->internal_req_buffer;
  plci->NL.X = plci->NData;
  plci->NL.ReqCh = 0;
  plci->NL.Req = plci->nl_req = (byte) N_UDATA;
  plci->adapter->request (&plci->NL);
}


static void xconnect_write_coefs (PLCI   *plci, word internal_command)
{

  dbug (1, dprintf ("[%06lx] %s,%d: xconnect_write_coefs %04x",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, internal_command));

  plci->li_write_command = internal_command;
  plci->li_write_channel = 0;
}


static byte xconnect_write_coefs_process (dword Id, PLCI   *plci, byte Rc)
{
  DIVA_CAPI_ADAPTER   *a;
  word w, n, i, j, r, s, to_ch;
  dword d;
  byte   *p;
  struct xconnect_transfer_address_s   *transfer_address;
  byte ch_map[MIXER_CHANNELS_BRI];

  dbug (1, dprintf ("[%06x] %s,%d: xconnect_write_coefs_process %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->li_write_channel));

  a = plci->adapter;
  if ((plci->li_bchannel_id == 0)
   || (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci != plci))
  {
    dbug (1, dprintf ("[%06x] %s,%d: Channel id wiped out",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    return (true);
  }
  i = a->li_base + (plci->li_bchannel_id - 1);
  j = plci->li_write_channel;
  p = plci->internal_req_buffer;
  if (j != 0)
  {
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: LI write coefs failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      return (false);
    }
  }
  if (li_config_table[i].adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
  {
    r = 0;
    s = 0;
    if (j < li_total_channels)
    {
      if (li_config_table[i].channel & LI_CHANNEL_ADDRESSES_SET)
      {
        s = ((li_config_table[i].send_b.card_address.low | li_config_table[i].send_b.card_address.high) ?
            (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_CH_PC | LI_COEF_PC_PC)) &
          ((li_config_table[i].send_pc.card_address.low | li_config_table[i].send_pc.card_address.high) ?
            (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_CH_CH | LI_COEF_PC_CH));
      }
      r = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
      while ((j < li_total_channels)
        && ((r == 0)
         || (!(li_config_table[j].channel & LI_CHANNEL_ADDRESSES_SET))
         || (!li_config_table[j].adapter->li_pri
          && (j >= li_config_table[j].adapter->li_base + MIXER_BCHANNELS_BRI))
         || (((li_config_table[j].send_b.card_address.low != li_config_table[i].send_b.card_address.low)
           || (li_config_table[j].send_b.card_address.high != li_config_table[i].send_b.card_address.high))
          && (!(a->manufacturer_features & MANUFACTURER_FEATURE_DMACONNECT)
           || !(li_config_table[j].adapter->manufacturer_features & MANUFACTURER_FEATURE_DMACONNECT)))
         || ((li_config_table[j].adapter->li_base != a->li_base)
          && !(r & s &
            ((li_config_table[j].send_b.card_address.low | li_config_table[j].send_b.card_address.high) ?
              (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_PC_CH | LI_COEF_PC_PC)) &
            ((li_config_table[j].send_pc.card_address.low | li_config_table[j].send_pc.card_address.high) ?
              (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_CH_CH | LI_COEF_CH_PC))))))
      {
        j++;
        if (j < li_total_channels)
          r = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
      }
    }
    if (j < li_total_channels)
    {
      plci->internal_command = plci->li_write_command;
      if (plci_nl_busy (plci))
        return (true);
      to_ch = (a->li_pri) ? plci->li_bchannel_id - 1 : 0;
      *(p++) = UDATA_REQUEST_XCONNECT_TO;
      do
      {
        if (li_config_table[j].adapter->li_base != a->li_base)
        {
          r &= s &
            ((li_config_table[j].send_b.card_address.low | li_config_table[j].send_b.card_address.high) ?
              (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_PC_CH | LI_COEF_PC_PC)) &
            ((li_config_table[j].send_pc.card_address.low | li_config_table[j].send_pc.card_address.high) ?
              (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_CH_CH | LI_COEF_CH_PC));
        }
        n = 0;
        do
        {
          if (r & xconnect_write_prog[n].mask)
          {
            if (xconnect_write_prog[n].from_pc)
              transfer_address = &(li_config_table[j].send_pc);
            else
              transfer_address = &(li_config_table[j].send_b);
            d = transfer_address->card_address.low;
            *(p++) = (byte) d;
            *(p++) = (byte)(d >> 8);
            *(p++) = (byte)(d >> 16);
            *(p++) = (byte)(d >> 24);
            d = transfer_address->card_address.high;
            *(p++) = (byte) d;
            *(p++) = (byte)(d >> 8);
            *(p++) = (byte)(d >> 16);
            *(p++) = (byte)(d >> 24);
            d = transfer_address->offset;
            *(p++) = (byte) d;
            *(p++) = (byte)(d >> 8);
            *(p++) = (byte)(d >> 16);
            *(p++) = (byte)(d >> 24);
            w = xconnect_write_prog[n].to_pc ? to_ch | XCONNECT_CHANNEL_PORT_PC : to_ch;
            *(p++) = (byte) w;
            *(p++) = (byte)(w >> 8);
            w = ((li_config_table[i].coef_table[j] & xconnect_write_prog[n].mask) == 0) ? 0x01 :
              (li_config_table[i].adapter->u_law ?
                 (li_config_table[j].adapter->u_law ? 0x80 : 0x86) :
                 (li_config_table[j].adapter->u_law ? 0x7a : 0x80));
            *(p++) = (byte) w;
            *(p++) = (byte) 0;
            li_config_table[i].coef_table[j] ^= xconnect_write_prog[n].mask << 4;
          }
          n++;
        } while ((n < ARRAY_SIZE(xconnect_write_prog))
          && ((p - plci->internal_req_buffer) + 16 < INTERNAL_REQ_BUFFER_SIZE));
        if (n == ARRAY_SIZE(xconnect_write_prog))
        {
          do
          {
            j++;
            if (j < li_total_channels)
              r = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
          } while ((j < li_total_channels)
            && ((r == 0)
             || (!(li_config_table[j].channel & LI_CHANNEL_ADDRESSES_SET))
             || (!li_config_table[j].adapter->li_pri
              && (j >= li_config_table[j].adapter->li_base + MIXER_BCHANNELS_BRI))
             || (((li_config_table[j].send_b.card_address.low != li_config_table[i].send_b.card_address.low)
               || (li_config_table[j].send_b.card_address.high != li_config_table[i].send_b.card_address.high))
              && (!(a->manufacturer_features & MANUFACTURER_FEATURE_DMACONNECT)
               || !(li_config_table[j].adapter->manufacturer_features & MANUFACTURER_FEATURE_DMACONNECT)))
             || ((li_config_table[j].adapter->li_base != a->li_base)
              && !(r & s &
                ((li_config_table[j].send_b.card_address.low | li_config_table[j].send_b.card_address.high) ?
                  (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_PC_CH | LI_COEF_PC_PC)) &
                ((li_config_table[j].send_pc.card_address.low | li_config_table[j].send_pc.card_address.high) ?
                  (LI_COEF_CH_CH | LI_COEF_CH_PC | LI_COEF_PC_CH | LI_COEF_PC_PC) : (LI_COEF_CH_CH | LI_COEF_CH_PC))))));
        }
      } while ((j < li_total_channels)
        && ((p - plci->internal_req_buffer) + 16 < INTERNAL_REQ_BUFFER_SIZE));
    }
    else if (j == li_total_channels)
    {
      plci->internal_command = plci->li_write_command;
      if (plci_nl_busy (plci))
        return (true);
      if (a->li_pri)
      {
        *(p++) = UDATA_REQUEST_SET_MIXER_COEFS_PRI_SYNC;
        w = 0;
        if (li_config_table[i].channel & LI_CHANNEL_TX_DATA)
          w |= MIXER_FEATURE_ENABLE_TX_DATA;
        if (li_config_table[i].channel & LI_CHANNEL_RX_DATA)
          w |= MIXER_FEATURE_ENABLE_RX_DATA;
        *(p++) = (byte) w;
        *(p++) = (byte)(w >> 8);
      }
      else
      {
        *(p++) = UDATA_REQUEST_SET_MIXER_COEFS_BRI;
        w = 0;
        if ((plci->tel == ADV_VOICE) && (plci == a->AdvSignalPLCI)
         && (ADV_VOICE_NEW_COEF_BASE + sizeof(word) <= a->adv_voice_coef_length))
        {
          w = GET_WORD (a->adv_voice_coef_buffer + ADV_VOICE_NEW_COEF_BASE);
        }
        if (li_config_table[i].channel & LI_CHANNEL_TX_DATA)
          w |= MIXER_FEATURE_ENABLE_TX_DATA;
        if (li_config_table[i].channel & LI_CHANNEL_RX_DATA)
          w |= MIXER_FEATURE_ENABLE_RX_DATA;
        *(p++) = (byte) w;
        *(p++) = (byte)(w >> 8);
        for (j = 0; j < sizeof(ch_map); j += 2)
        {
          if (plci->li_bchannel_id == 2)
          {
            ch_map[j] = (byte)(j+1);
            ch_map[j+1] = (byte) j;
          }
          else
          {
            ch_map[j] = (byte) j;
            ch_map[j+1] = (byte)(j+1);
          }
        }
        for (n = 0; n < ARRAY_SIZE(mixer_write_prog_bri); n++)
        {
          i = a->li_base + ch_map[mixer_write_prog_bri[n].to_ch];
          j = a->li_base + ch_map[mixer_write_prog_bri[n].from_ch];
          if (li_config_table[i].channel & li_config_table[j].channel & LI_CHANNEL_INVOLVED)
          {
            *p = (mixer_write_prog_bri[n].xconnect_override != 0) ?
              mixer_write_prog_bri[n].xconnect_override :
              ((li_config_table[i].coef_table[j] & mixer_write_prog_bri[n].mask) ? 0x80 : 0x01);
            if ((i >= a->li_base + MIXER_BCHANNELS_BRI) || (j >= a->li_base + MIXER_BCHANNELS_BRI))
            {
              w = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
              li_config_table[i].coef_table[j] ^= (w & mixer_write_prog_bri[n].mask) << 4;
            }
          }
          else
          {
            *p = 0x00;
            if ((a->AdvSignalPLCI != NULL) && (a->AdvSignalPLCI->tel == ADV_VOICE))
            {
              w = (plci == a->AdvSignalPLCI) ? n : mixer_swapped_index_bri[n];
              if (ADV_VOICE_NEW_COEF_BASE + sizeof(word) + w < a->adv_voice_coef_length)
                *p = a->adv_voice_coef_buffer[ADV_VOICE_NEW_COEF_BASE + sizeof(word) + w];
            }
          }
          p++;
        }
      }
      j = li_total_channels + 1;
    }
  }
  else
  {
    if (j <= li_total_channels)
    {
      plci->internal_command = plci->li_write_command;
      if (plci_nl_busy (plci))
        return (true);
      if (j < a->li_base)
        j = a->li_base;
      if (a->li_pri)
      {
        *(p++) = UDATA_REQUEST_SET_MIXER_COEFS_PRI_SYNC;
        w = 0;
        if (li_config_table[i].channel & LI_CHANNEL_TX_DATA)
          w |= MIXER_FEATURE_ENABLE_TX_DATA;
        if (li_config_table[i].channel & LI_CHANNEL_RX_DATA)
          w |= MIXER_FEATURE_ENABLE_RX_DATA;
        *(p++) = (byte) w;
        *(p++) = (byte)(w >> 8);
        for (n = 0; n < ARRAY_SIZE(mixer_write_prog_pri); n++)
        {
          *(p++) = (byte)((plci->li_bchannel_id - 1) | mixer_write_prog_pri[n].line_flags);
          for (j = a->li_base; j < a->li_base + MIXER_CHANNELS_PRI; j++)
          {
            w = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
            if (w & mixer_write_prog_pri[n].mask)
            {
              *(p++) = (li_config_table[i].coef_table[j] & mixer_write_prog_pri[n].mask) ? 0x80 : 0x01;
              li_config_table[i].coef_table[j] ^= mixer_write_prog_pri[n].mask << 4;
            }
            else
              *(p++) = 0x00;
          }
          *(p++) = (byte)((plci->li_bchannel_id - 1) | MIXER_COEF_LINE_ROW_FLAG | mixer_write_prog_pri[n].line_flags);
          for (j = a->li_base; j < a->li_base + MIXER_CHANNELS_PRI; j++)
          {
            w = ((li_config_table[j].coef_table[i] & 0xf) ^ (li_config_table[j].coef_table[i] >> 4));
            if (w & mixer_write_prog_pri[n].mask)
            {
              *(p++) = (li_config_table[j].coef_table[i] & mixer_write_prog_pri[n].mask) ? 0x80 : 0x01;
              li_config_table[j].coef_table[i] ^= mixer_write_prog_pri[n].mask << 4;
            }
            else
              *(p++) = 0x00;
          }
        }
      }
      else
      {
        *(p++) = UDATA_REQUEST_SET_MIXER_COEFS_BRI;
        w = 0;
        if ((plci->tel == ADV_VOICE) && (plci == a->AdvSignalPLCI)
         && (ADV_VOICE_NEW_COEF_BASE + sizeof(word) <= a->adv_voice_coef_length))
        {
          w = GET_WORD (a->adv_voice_coef_buffer + ADV_VOICE_NEW_COEF_BASE);
        }
        if (li_config_table[i].channel & LI_CHANNEL_TX_DATA)
          w |= MIXER_FEATURE_ENABLE_TX_DATA;
        if (li_config_table[i].channel & LI_CHANNEL_RX_DATA)
          w |= MIXER_FEATURE_ENABLE_RX_DATA;
        *(p++) = (byte) w;
        *(p++) = (byte)(w >> 8);
        for (j = 0; j < sizeof(ch_map); j += 2)
        {
          if (plci->li_bchannel_id == 2)
          {
            ch_map[j] = (byte)(j+1);
            ch_map[j+1] = (byte) j;
          }
          else
          {
            ch_map[j] = (byte) j;
            ch_map[j+1] = (byte)(j+1);
          }
        }
        for (n = 0; n < ARRAY_SIZE(mixer_write_prog_bri); n++)
        {
          i = a->li_base + ch_map[mixer_write_prog_bri[n].to_ch];
          j = a->li_base + ch_map[mixer_write_prog_bri[n].from_ch];
          if (li_config_table[i].channel & li_config_table[j].channel & LI_CHANNEL_INVOLVED)
          {
            *p = ((li_config_table[i].coef_table[j] & mixer_write_prog_bri[n].mask) ? 0x80 : 0x01);
            w = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
            li_config_table[i].coef_table[j] ^= (w & mixer_write_prog_bri[n].mask) << 4;
          }
          else
          {
            *p = 0x00;
            if ((a->AdvSignalPLCI != NULL) && (a->AdvSignalPLCI->tel == ADV_VOICE))
            {
              w = (plci == a->AdvSignalPLCI) ? n : mixer_swapped_index_bri[n];
              if (ADV_VOICE_NEW_COEF_BASE + sizeof(word) + w < a->adv_voice_coef_length)
                *p = a->adv_voice_coef_buffer[ADV_VOICE_NEW_COEF_BASE + sizeof(word) + w];
            }
          }
          p++;
        }
      }
      j = li_total_channels + 1;
    }
  }
  plci->li_write_channel = j;
  if (p != plci->internal_req_buffer)
  {
    plci->NData[0].P = plci->internal_req_buffer;
    plci->NData[0].PLength = p - plci->internal_req_buffer;
    plci->NL.X = plci->NData;
    plci->NL.ReqCh = 0;
    plci->NL.Req = plci->nl_req = (byte) N_UDATA;
    plci->adapter->request (&plci->NL);
  }
  return (true);
}


static void mixer_notify_update (PLCI   *plci, byte others)
{
  DIVA_CAPI_ADAPTER   *a;
  word i, w;
  PLCI   *notify_plci;
    byte msg[sizeof(CAPI_MSG_HEADER) + 6];

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_notify_update %d",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, others));

  a = plci->adapter;
  if (a->profile.Global_Options & GL_LINE_INTERCONNECT_SUPPORTED)
  {
    if (others)
      plci->li_notify_update = true;
    i = 0;
    do
    {
      notify_plci = NULL;
      if (others)
      {
        while ((i < li_total_channels) && (li_config_table[i].plci == NULL))
          i++;
        if (i < li_total_channels)
          notify_plci = li_config_table[i++].plci;
      }
      else
      {
        if ((plci->li_bchannel_id != 0)
         && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
        {
          notify_plci = plci;
        }
      }
      if ((notify_plci != NULL)
       && !notify_plci->li_notify_update
       && (notify_plci->appl != NULL)
       && (notify_plci->State)
       && notify_plci->NL.Id && !notify_plci->nl_remove_id)
      {
        notify_plci->li_notify_update = true;
        ((CAPI_MSG *) msg)->header.length = 18;
        ((CAPI_MSG *) msg)->header.appl_id = notify_plci->appl->Id;
        ((CAPI_MSG *) msg)->header.command = _FACILITY_R;
        ((CAPI_MSG *) msg)->header.number = 0;
        ((CAPI_MSG *) msg)->header.controller = notify_plci->adapter->Id;
        ((CAPI_MSG *) msg)->header.plci = notify_plci->Id;
        ((CAPI_MSG *) msg)->header.ncci = 0;
        ((CAPI_MSG *) msg)->info.facility_req.Selector = SELECTOR_LINE_INTERCONNECT;
        ((CAPI_MSG *) msg)->info.facility_req.structs[0] = 3;
        PUT_WORD (&(((CAPI_MSG *) msg)->info.facility_req.structs[1]), LI_REQ_SILENT_UPDATE);
        ((CAPI_MSG *) msg)->info.facility_req.structs[3] = 0;
        w = api_put (notify_plci->appl, (CAPI_MSG *) msg);
        if (w != _QUEUE_FULL)
        {
          if (w != 0)
          {
            dbug (1, dprintf ("[%06lx] %s,%d: Interconnect notify failed %06x %d",
              (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
              (char   *)(FILE_), __LINE__,
              (dword)((notify_plci->Id << 8) | UnMapController (notify_plci->adapter->Id)), w));
          }
          notify_plci->li_notify_update = false;
        }
      }
    } while (others && (notify_plci != NULL));
    if (others)
      plci->li_notify_update = false;
  }
}


static void mixer_clear_config (PLCI   *plci)
{
  DIVA_CAPI_ADAPTER   *a;
  word i, j;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_clear_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->li_notify_update = false;
  plci->li_plci_b_write_pos = 0;
  plci->li_plci_b_read_pos = 0;
  plci->li_plci_b_req_pos = 0;
  a = plci->adapter;
  if ((plci->li_bchannel_id != 0)
   && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    i = a->li_base + (plci->li_bchannel_id - 1);
    li_config_table[i].curchnl = 0;
    li_config_table[i].channel = 0;
    li_config_table[i].chflags = 0;
    for (j = 0; j < li_total_channels; j++)
    {
      li_config_table[j].flag_table[i] = 0;
      li_config_table[i].flag_table[j] = 0;
      li_config_table[i].coef_table[j] = 0;
      li_config_table[j].coef_table[i] = 0;
    }
    if (!a->li_pri)
    {
      li_config_table[i].coef_table[i] |= LI_COEF_CH_PC_SET | LI_COEF_PC_CH_SET;
      if ((plci->tel == ADV_VOICE) && (plci == a->AdvSignalPLCI))
      {
        i = a->li_base + MIXER_IC_CHANNEL_BASE + (plci->li_bchannel_id - 1);
        li_config_table[i].curchnl = 0;
        li_config_table[i].channel = 0;
        li_config_table[i].chflags = 0;
        for (j = 0; j < li_total_channels; j++)
        {
          li_config_table[i].flag_table[j] = 0;
          li_config_table[j].flag_table[i] = 0;
          li_config_table[i].coef_table[j] = 0;
          li_config_table[j].coef_table[i] = 0;
        }
        if (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC)
        {
          i = a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id);
          li_config_table[i].curchnl = 0;
          li_config_table[i].channel = 0;
          li_config_table[i].chflags = 0;
          for (j = 0; j < li_total_channels; j++)
          {
            li_config_table[i].flag_table[j] = 0;
            li_config_table[j].flag_table[i] = 0;
            li_config_table[i].coef_table[j] = 0;
            li_config_table[j].coef_table[i] = 0;
          }
        }
      }
    }
  }
}


static void mixer_prepare_switch (dword Id, PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_prepare_switch",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  do
  {
    mixer_indication_coefs_set (Id, plci);
  } while (plci->li_plci_b_read_pos != plci->li_plci_b_req_pos);
}


static word mixer_save_config (dword Id, PLCI   *plci, byte Rc)
{
  DIVA_CAPI_ADAPTER   *a;
  word i, j;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_save_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  a = plci->adapter;
  if ((plci->li_bchannel_id != 0)
   && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    i = a->li_base + (plci->li_bchannel_id - 1);
    for (j = 0; j < li_total_channels; j++)
    {
      li_config_table[i].coef_table[j] &= 0xf;
      li_config_table[j].coef_table[i] &= 0xf;
    }
    if (!a->li_pri)
      li_config_table[i].coef_table[i] |= LI_COEF_CH_PC_SET | LI_COEF_PC_CH_SET;
  }
  return (GOOD);
}


static word mixer_restore_config (dword Id, PLCI   *plci, byte Rc)
{
  DIVA_CAPI_ADAPTER   *a;
  word Info;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_restore_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  Info = GOOD;
  a = plci->adapter;
  if ((plci->B1_facilities & B1_FACILITY_MIXER)
   && (plci->li_bchannel_id != 0)
   && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    switch (plci->adjust_b_state)
    {
    case ADJUST_B_RESTORE_MIXER_1:
      if (a->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
      {
        plci->internal_command = plci->adjust_b_command;
        if (plci_nl_busy (plci))
        {
          plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_1;
          break;
        }
        xconnect_query_addresses (plci);
        plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_2;
        break;
      }
      plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_5;
      Rc = OK;
    case ADJUST_B_RESTORE_MIXER_2:
    case ADJUST_B_RESTORE_MIXER_3:
    case ADJUST_B_RESTORE_MIXER_4:
      if ((Rc != OK) && (Rc != OK_FC) && (Rc != 0))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Adjust B query addresses failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _WRONG_STATE;
        break;
      }
      if (Rc == OK)
      {
        if (plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_2)
          plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_3;
        else if (plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_4)
          plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_5;
      }
      else if (Rc == 0)
      {
        if (plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_2)
          plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_4;
        else if (plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_3)
          plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_5;
      }
      if (plci->adjust_b_state != ADJUST_B_RESTORE_MIXER_5)
      {
        plci->internal_command = plci->adjust_b_command;
        break;
      }
    case ADJUST_B_RESTORE_MIXER_5:
      xconnect_write_coefs (plci, plci->adjust_b_command);
      plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_6;
      Rc = OK;
    case ADJUST_B_RESTORE_MIXER_6:
      if (!xconnect_write_coefs_process (Id, plci, Rc))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Write mixer coefs failed",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      if (plci->internal_command)
        break;
      plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_7;
    case ADJUST_B_RESTORE_MIXER_7:
      break;
    }
  }
  return (Info);
}


static void mixer_command (dword Id, PLCI   *plci, byte Rc)
{
  DIVA_CAPI_ADAPTER   *a;
  word i, internal_command, Info;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_command %02x %04x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command,
    plci->li_cmd));

  Info = GOOD;
  a = plci->adapter;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (plci->li_cmd)
  {
  case LI_REQ_CONNECT:
  case LI_REQ_DISCONNECT:
  case LI_REQ_SILENT_UPDATE:
    switch (internal_command)
    {
    default:
      if (plci->li_channel_bits & LI_CHANNEL_INVOLVED)
      {
        adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities |
          B1_FACILITY_MIXER), MIXER_COMMAND_1);
      }
    case MIXER_COMMAND_1:
      if (plci->li_channel_bits & LI_CHANNEL_INVOLVED)
      {
        if (adjust_b_process (Id, plci, Rc) != GOOD)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Load mixer failed",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _FACILITY_NOT_SUPPORTED;
          break;
        }
        if (plci->internal_command)
          return;
      }
      plci->li_plci_b_req_pos = plci->li_plci_b_write_pos;
      if ((plci->li_channel_bits & LI_CHANNEL_INVOLVED)
       || ((get_b1_facilities (plci, plci->B1_resource) & B1_FACILITY_MIXER)
        && (add_b1_facilities (plci, plci->B1_resource, (word)(plci->B1_facilities &
         ~B1_FACILITY_MIXER)) == plci->B1_resource)))
      {
        xconnect_write_coefs (plci, MIXER_COMMAND_2);
      }
      else
      {
        do
        {
          mixer_indication_coefs_set (Id, plci);
        } while (plci->li_plci_b_read_pos != plci->li_plci_b_req_pos);
      }
    case MIXER_COMMAND_2:
      if ((plci->li_channel_bits & LI_CHANNEL_INVOLVED)
       || ((get_b1_facilities (plci, plci->B1_resource) & B1_FACILITY_MIXER)
        && (add_b1_facilities (plci, plci->B1_resource, (word)(plci->B1_facilities &
         ~B1_FACILITY_MIXER)) == plci->B1_resource)))
      {
        if (!xconnect_write_coefs_process (Id, plci, Rc))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Write mixer coefs failed",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          if (plci->li_plci_b_write_pos != plci->li_plci_b_req_pos)
          {
            do
            {
              plci->li_plci_b_write_pos = (plci->li_plci_b_write_pos == 0) ?
                LI_PLCI_B_QUEUE_ENTRIES-1 : plci->li_plci_b_write_pos - 1;
              i = (plci->li_plci_b_write_pos == 0) ?
                LI_PLCI_B_QUEUE_ENTRIES-1 : plci->li_plci_b_write_pos - 1;
            } while ((plci->li_plci_b_write_pos != plci->li_plci_b_req_pos)
              && !(plci->li_plci_b_queue[i] & LI_PLCI_B_LAST_FLAG));
          }
          Info = _FACILITY_NOT_SUPPORTED;
          break;
        }
        if (plci->internal_command)
          return;
      }
      if (!(plci->li_channel_bits & LI_CHANNEL_INVOLVED))
      {
        adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities &
          ~B1_FACILITY_MIXER), MIXER_COMMAND_3);
      }
    case MIXER_COMMAND_3:
      if (!(plci->li_channel_bits & LI_CHANNEL_INVOLVED))
      {
        if (adjust_b_process (Id, plci, Rc) != GOOD)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Unload mixer failed",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _FACILITY_NOT_SUPPORTED;
          break;
        }
        if (plci->internal_command)
          return;
      }
      break;
    }
    break;
  }
  if ((plci->li_bchannel_id == 0)
   || (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci != plci))
  {
    dbug (1, dprintf ("[%06x] %s,%d: Channel id wiped out %d",
      UnMapId (Id), (char   *)(FILE_), __LINE__, (int)(plci->li_bchannel_id)));
  }
  else
  {
    i = a->li_base + (plci->li_bchannel_id - 1);
    li_config_table[i].curchnl = plci->li_channel_bits;
    if (!a->li_pri && (plci->tel == ADV_VOICE) && (plci == a->AdvSignalPLCI))
    {
      i = a->li_base + MIXER_IC_CHANNEL_BASE + (plci->li_bchannel_id - 1);
      li_config_table[i].curchnl = plci->li_channel_bits;
      if (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC)
      {
        i = a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id);
        li_config_table[i].curchnl = plci->li_channel_bits;
      }
    }
  }
}


static void li_update_connect (dword Id, DIVA_CAPI_ADAPTER   *a, PLCI   *plci,
  dword plci_b_id, byte connect, dword li_flags)
{
  word i, ch_a, ch_a_v, ch_a_s, ch_b, ch_b_v, ch_b_s;
  PLCI   *plci_b;
  DIVA_CAPI_ADAPTER   *a_b;

  a_b = &(adapter[MapController ((byte)(plci_b_id & 0x7f)) - 1]);
  plci_b = &(a_b->plci[((plci_b_id >> 8) & 0xff) - 1]);
  ch_a = a->li_base + (plci->li_bchannel_id - 1);
  if (!a->li_pri && (plci->tel == ADV_VOICE)
   && (plci == a->AdvSignalPLCI) && (Id & EXT_CONTROLLER))
  {
    ch_a_v = ch_a + MIXER_IC_CHANNEL_BASE;
    ch_a_s = (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC) ?
      a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id) : ch_a_v;
  }
  else
  {
    ch_a_v = ch_a;
    ch_a_s = ch_a;
  }
  ch_b = a_b->li_base + (plci_b->li_bchannel_id - 1);
  if (!a_b->li_pri && (plci_b->tel == ADV_VOICE)
   && (plci_b == a_b->AdvSignalPLCI) && (plci_b_id & EXT_CONTROLLER))
  {
    ch_b_v = ch_b + MIXER_IC_CHANNEL_BASE;
    ch_b_s = (a_b->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC) ?
      a_b->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci_b->li_bchannel_id) : ch_b_v;
  }
  else
  {
    ch_b_v = ch_b;
    ch_b_s = ch_b;
  }
  if (connect)
  {
    li_config_table[ch_a].flag_table[ch_a_v] &= ~LI_FLAG_MONITOR;
    li_config_table[ch_a].flag_table[ch_a_s] &= ~LI_FLAG_MONITOR;
    li_config_table[ch_a_v].flag_table[ch_a] &= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);
    li_config_table[ch_a_s].flag_table[ch_a] &= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);
  }
  li_config_table[ch_a].flag_table[ch_b_v] &= ~LI_FLAG_MONITOR;
  li_config_table[ch_a].flag_table[ch_b_s] &= ~LI_FLAG_MONITOR;
  li_config_table[ch_b_v].flag_table[ch_a] &= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);
  li_config_table[ch_b_s].flag_table[ch_a] &= ~(LI_FLAG_ANNOUNCEMENT | LI_FLAG_MIX);
  if (ch_a_v == ch_b_v)
  {
    li_config_table[ch_a_v].flag_table[ch_b_v] &= ~LI_FLAG_CONFERENCE;
    li_config_table[ch_a_s].flag_table[ch_b_s] &= ~LI_FLAG_CONFERENCE;
  }
  else
  {
    if (li_config_table[ch_a_v].flag_table[ch_b_v] & LI_FLAG_CONFERENCE)
    {
      for (i = 0; i < li_total_channels; i++)
      {
        if (i != ch_a_v)
          li_config_table[ch_a_v].flag_table[i] &= ~LI_FLAG_CONFERENCE;
      }
    }
    if (li_config_table[ch_a_s].flag_table[ch_b_v] & LI_FLAG_CONFERENCE)
    {
      for (i = 0; i < li_total_channels; i++)
      {
        if (i != ch_a_s)
          li_config_table[ch_a_s].flag_table[i] &= ~LI_FLAG_CONFERENCE;
      }
    }
    if (li_config_table[ch_b_v].flag_table[ch_a_v] & LI_FLAG_CONFERENCE)
    {
      for (i = 0; i < li_total_channels; i++)
      {
        if (i != ch_a_v)
          li_config_table[i].flag_table[ch_a_v] &= ~LI_FLAG_CONFERENCE;
      }
    }
    if (li_config_table[ch_b_v].flag_table[ch_a_s] & LI_FLAG_CONFERENCE)
    {
      for (i = 0; i < li_total_channels; i++)
      {
        if (i != ch_a_s)
          li_config_table[i].flag_table[ch_a_s] &= ~LI_FLAG_CONFERENCE;
      }
    }
  }
  if (li_flags & LI_FLAG_CONFERENCE_A_B)
  {
    li_config_table[ch_b_v].flag_table[ch_a_v] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_b_s].flag_table[ch_a_v] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_b_v].flag_table[ch_a_s] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_b_s].flag_table[ch_a_s] |= LI_FLAG_CONFERENCE;
  }
  if (li_flags & LI_FLAG_CONFERENCE_B_A)
  {
    li_config_table[ch_a_v].flag_table[ch_b_v] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_a_v].flag_table[ch_b_s] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_a_s].flag_table[ch_b_v] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_a_s].flag_table[ch_b_s] |= LI_FLAG_CONFERENCE;
  }
  if (li_flags & LI_FLAG_MONITOR_A)
  {
    li_config_table[ch_a].flag_table[ch_a_v] |= LI_FLAG_MONITOR;
    li_config_table[ch_a].flag_table[ch_a_s] |= LI_FLAG_MONITOR;
  }
  if (li_flags & LI_FLAG_MONITOR_B)
  {
    li_config_table[ch_a].flag_table[ch_b_v] |= LI_FLAG_MONITOR;
    li_config_table[ch_a].flag_table[ch_b_s] |= LI_FLAG_MONITOR;
  }
  if (li_flags & LI_FLAG_ANNOUNCEMENT_A)
  {
    li_config_table[ch_a_v].flag_table[ch_a] |= LI_FLAG_ANNOUNCEMENT;
    li_config_table[ch_a_s].flag_table[ch_a] |= LI_FLAG_ANNOUNCEMENT;
  }
  if (li_flags & LI_FLAG_ANNOUNCEMENT_B)
  {
    li_config_table[ch_b_v].flag_table[ch_a] |= LI_FLAG_ANNOUNCEMENT;
    li_config_table[ch_b_s].flag_table[ch_a] |= LI_FLAG_ANNOUNCEMENT;
  }
  if (li_flags & LI_FLAG_MIX_A)
  {
    li_config_table[ch_a_v].flag_table[ch_a] |= LI_FLAG_MIX;
    li_config_table[ch_a_s].flag_table[ch_a] |= LI_FLAG_MIX;
  }
  if (li_flags & LI_FLAG_MIX_B)
  {
    li_config_table[ch_b_v].flag_table[ch_a] |= LI_FLAG_MIX;
    li_config_table[ch_b_s].flag_table[ch_a] |= LI_FLAG_MIX;
  }
  if (ch_a_v != ch_a_s)
  {
    li_config_table[ch_a_v].flag_table[ch_a_s] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_a_s].flag_table[ch_a_v] |= LI_FLAG_CONFERENCE;
  }
  if (ch_b_v != ch_b_s)
  {
    li_config_table[ch_b_v].flag_table[ch_b_s] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_b_s].flag_table[ch_b_v] |= LI_FLAG_CONFERENCE;
  }
}


static void li2_update_connect (dword Id, DIVA_CAPI_ADAPTER   *a, PLCI   *plci,
  dword plci_b_id, byte connect, dword li_flags)
{
  word ch_a, ch_a_v, ch_a_s, ch_b, ch_b_v, ch_b_s;
  PLCI   *plci_b;
  DIVA_CAPI_ADAPTER   *a_b;

  a_b = &(adapter[MapController ((byte)(plci_b_id & 0x7f)) - 1]);
  plci_b = &(a_b->plci[((plci_b_id >> 8) & 0xff) - 1]);
  ch_a = a->li_base + (plci->li_bchannel_id - 1);
  if (!a->li_pri && (plci->tel == ADV_VOICE)
   && (plci == a->AdvSignalPLCI) && (Id & EXT_CONTROLLER))
  {
    ch_a_v = ch_a + MIXER_IC_CHANNEL_BASE;
    ch_a_s = (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC) ?
      a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id) : ch_a_v;
  }
  else
  {
    ch_a_v = ch_a;
    ch_a_s = ch_a;
  }
  ch_b = a_b->li_base + (plci_b->li_bchannel_id - 1);
  if (!a_b->li_pri && (plci_b->tel == ADV_VOICE)
   && (plci_b == a_b->AdvSignalPLCI) && (plci_b_id & EXT_CONTROLLER))
  {
    ch_b_v = ch_b + MIXER_IC_CHANNEL_BASE;
    ch_b_s = (a_b->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC) ?
      a_b->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci_b->li_bchannel_id) : ch_b_v;
  }
  else
  {
    ch_b_v = ch_b;
    ch_b_s = ch_b;
  }
  if (connect)
  {
    li_config_table[ch_b].flag_table[ch_b_v] &= ~LI_FLAG_MONITOR;
    li_config_table[ch_b].flag_table[ch_b_s] &= ~LI_FLAG_MONITOR;
    li_config_table[ch_b_v].flag_table[ch_b] &= ~LI_FLAG_MIX;
    li_config_table[ch_b_s].flag_table[ch_b] &= ~LI_FLAG_MIX;
    li_config_table[ch_b].flag_table[ch_b] &= ~LI_FLAG_PCCONNECT;
    li_config_table[ch_b].chflags &= ~(LI_CHFLAG_MONITOR | LI_CHFLAG_MIX | LI_CHFLAG_LOOP);
  }
  li_config_table[ch_b_v].flag_table[ch_a_v] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_b_s].flag_table[ch_a_v] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_b_v].flag_table[ch_a_s] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_b_s].flag_table[ch_a_s] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_a_v].flag_table[ch_b_v] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_a_v].flag_table[ch_b_s] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_a_s].flag_table[ch_b_v] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  li_config_table[ch_a_s].flag_table[ch_b_s] &= ~(LI_FLAG_INTERCONNECT | LI_FLAG_CONFERENCE);
  if (li_flags & LI2_FLAG_INTERCONNECT_A_B)
  {
    li_config_table[ch_b_v].flag_table[ch_a_v] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_b_s].flag_table[ch_a_v] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_b_v].flag_table[ch_a_s] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_b_s].flag_table[ch_a_s] |= LI_FLAG_INTERCONNECT;
  }
  if (li_flags & LI2_FLAG_INTERCONNECT_B_A)
  {
    li_config_table[ch_a_v].flag_table[ch_b_v] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_a_v].flag_table[ch_b_s] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_a_s].flag_table[ch_b_v] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_a_s].flag_table[ch_b_s] |= LI_FLAG_INTERCONNECT;
  }
  if (li_flags & LI2_FLAG_MONITOR_B)
  {
    li_config_table[ch_b].flag_table[ch_b_v] |= LI_FLAG_MONITOR;
    li_config_table[ch_b].flag_table[ch_b_s] |= LI_FLAG_MONITOR;
  }
  if (li_flags & LI2_FLAG_MIX_B)
  {
    li_config_table[ch_b_v].flag_table[ch_b] |= LI_FLAG_MIX;
    li_config_table[ch_b_s].flag_table[ch_b] |= LI_FLAG_MIX;
  }
  if (li_flags & LI2_FLAG_MONITOR_X)
    li_config_table[ch_b].chflags |= LI_CHFLAG_MONITOR;
  if (li_flags & LI2_FLAG_MIX_X)
    li_config_table[ch_b].chflags |= LI_CHFLAG_MIX;
  if (li_flags & LI2_FLAG_LOOP_B)
  {
    li_config_table[ch_b_v].flag_table[ch_b_v] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_b_s].flag_table[ch_b_v] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_b_v].flag_table[ch_b_s] |= LI_FLAG_INTERCONNECT;
    li_config_table[ch_b_s].flag_table[ch_b_s] |= LI_FLAG_INTERCONNECT;
  }
  if (li_flags & LI2_FLAG_LOOP_PC)
    li_config_table[ch_b].flag_table[ch_b] |= LI_FLAG_PCCONNECT;
  if (li_flags & LI2_FLAG_LOOP_X)
    li_config_table[ch_b].chflags |= LI_CHFLAG_LOOP;
  if (li_flags & LI2_FLAG_PCCONNECT_A_B)
    li_config_table[ch_b_s].flag_table[ch_a_s] |= LI_FLAG_PCCONNECT;
  if (li_flags & LI2_FLAG_PCCONNECT_B_A)
    li_config_table[ch_a_s].flag_table[ch_b_s] |= LI_FLAG_PCCONNECT;
  if (ch_a_v != ch_a_s)
  {
    li_config_table[ch_a_v].flag_table[ch_a_s] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_a_s].flag_table[ch_a_v] |= LI_FLAG_CONFERENCE;
  }
  if (ch_b_v != ch_b_s)
  {
    li_config_table[ch_b_v].flag_table[ch_b_s] |= LI_FLAG_CONFERENCE;
    li_config_table[ch_b_s].flag_table[ch_b_v] |= LI_FLAG_CONFERENCE;
  }
}


static word li_check_main_plci (dword Id, PLCI   *plci)
{
  if (plci == NULL)
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Wrong PLCI",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    return (_WRONG_IDENTIFIER);
  }
  if (!plci->State
   || !plci->NL.Id || plci->nl_remove_id
   || (plci->li_bchannel_id == 0))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Wrong state",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    return (_WRONG_STATE);
  }
  li_config_table[plci->adapter->li_base + (plci->li_bchannel_id - 1)].plci = plci;
  return (GOOD);
}


static PLCI   *li_check_plci_b (dword Id, PLCI   *plci,
  dword plci_b_id, word plci_b_write_pos, byte   *p_result)
{
  byte ctlr_b;
  PLCI   *plci_b;

  if (((plci->li_plci_b_read_pos > plci_b_write_pos) ? plci->li_plci_b_read_pos :
    LI_PLCI_B_QUEUE_ENTRIES + plci->li_plci_b_read_pos) - plci_b_write_pos - 1 < 2)
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI request overrun",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    PUT_WORD (p_result, _REQUEST_NOT_ALLOWED_IN_THIS_STATE);
    return (NULL);
  }
  ctlr_b = 0;
  if ((plci_b_id & 0x7f) != 0)
  {
    ctlr_b = MapController ((byte)(plci_b_id & 0x7f));
    if ((ctlr_b > max_adapter) || ((ctlr_b != 0) && (adapter[ctlr_b - 1].request == NULL)))
      ctlr_b = 0;
  }
  if ((ctlr_b == 0)
   || (((plci_b_id >> 8) & 0xff) == 0)
   || (((plci_b_id >> 8) & 0xff) > adapter[ctlr_b - 1].max_plci))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI invalid second PLCI %08lx",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b_id));
    PUT_WORD (p_result, _WRONG_IDENTIFIER);
    return (NULL);
  }
  plci_b = &(adapter[ctlr_b - 1].plci[((plci_b_id >> 8) & 0xff) - 1]);
  if (!plci_b->State
   || !plci_b->NL.Id || plci_b->nl_remove_id
   || (plci_b->li_bchannel_id == 0))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI peer in wrong state %08lx",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b_id));
    PUT_WORD (p_result, _REQUEST_NOT_ALLOWED_IN_THIS_STATE);
    return (NULL);
  }
  li_config_table[plci_b->adapter->li_base + (plci_b->li_bchannel_id - 1)].plci = plci_b;
  if (((byte)(plci_b_id & ~EXT_CONTROLLER)) !=
    ((byte)(UnMapController (plci->adapter->Id) & ~EXT_CONTROLLER))
   && (!(plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
    || !(plci_b->adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI not on same ctrl %08lx",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b_id));
    PUT_WORD (p_result, _WRONG_IDENTIFIER);
    return (NULL);
  }
  if (!(get_b1_facilities (plci_b, add_b1_facilities (plci_b, plci_b->B1_resource,
    (word)(plci_b->B1_facilities | B1_FACILITY_MIXER))) & B1_FACILITY_MIXER))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Interconnect peer cannot mix %d",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b->B1_resource));
    PUT_WORD (p_result, _REQUEST_NOT_ALLOWED_IN_THIS_STATE);
    return (NULL);
  }
  return (plci_b);
}


static PLCI   *li2_check_plci_b (dword Id, PLCI   *plci,
  dword plci_b_id, word plci_b_write_pos, byte   *p_result)
{
  byte ctlr_b;
  PLCI   *plci_b;

  if (((plci->li_plci_b_read_pos > plci_b_write_pos) ? plci->li_plci_b_read_pos :
    LI_PLCI_B_QUEUE_ENTRIES + plci->li_plci_b_read_pos) - plci_b_write_pos - 1 < 2)
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI request overrun",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    PUT_WORD (p_result, _WRONG_STATE);
    return (NULL);
  }
  ctlr_b = 0;
  if ((plci_b_id & 0x7f) != 0)
  {
    ctlr_b = MapController ((byte)(plci_b_id & 0x7f));
    if ((ctlr_b > max_adapter) || ((ctlr_b != 0) && (adapter[ctlr_b - 1].request == NULL)))
      ctlr_b = 0;
  }
  if ((ctlr_b == 0)
   || (((plci_b_id >> 8) & 0xff) == 0)
   || (((plci_b_id >> 8) & 0xff) > adapter[ctlr_b - 1].max_plci))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI invalid second PLCI %08lx",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b_id));
    PUT_WORD (p_result, _WRONG_IDENTIFIER);
    return (NULL);
  }
  plci_b = &(adapter[ctlr_b - 1].plci[((plci_b_id >> 8) & 0xff) - 1]);
  if (!plci_b->State
   || !plci_b->NL.Id || plci_b->nl_remove_id
   || (plci_b->li_bchannel_id == 0)
   || (li_config_table[plci_b->adapter->li_base + (plci_b->li_bchannel_id - 1)].plci != plci_b))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI peer in wrong state %08lx",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b_id));
    PUT_WORD (p_result, _WRONG_STATE);
    return (NULL);
  }
  if (((byte)(plci_b_id & ~EXT_CONTROLLER)) !=
    ((byte)(UnMapController (plci->adapter->Id) & ~EXT_CONTROLLER))
   && (!(plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
    || !(plci_b->adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI not on same ctrl %08lx",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b_id));
    PUT_WORD (p_result, _WRONG_IDENTIFIER);
    return (NULL);
  }
  if (!(get_b1_facilities (plci_b, add_b1_facilities (plci_b, plci_b->B1_resource,
    (word)(plci_b->B1_facilities | B1_FACILITY_MIXER))) & B1_FACILITY_MIXER))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Interconnect peer cannot mix %d",
      UnMapId (Id), (char   *)(FILE_), __LINE__, plci_b->B1_resource));
    PUT_WORD (p_result, _WRONG_STATE);
    return (NULL);
  }
  return (plci_b);
}


static byte mixer_request (dword Id, word Number, DIVA_CAPI_ADAPTER   *a, PLCI   *plci, APPL   *appl, API_PARSE *msg)
{
  word Info;
  word i;
  dword d, li_flags, plci_b_id;
  PLCI   *plci_b;
    API_PARSE li_parms[3];
    API_PARSE li_req_parms[3];
    API_PARSE li_participant_struct[2];
    API_PARSE li_participant_parms[3];
  word participant_parms_pos;
  byte result_buffer[32];
  byte   *result;
  word result_pos;
  word plci_b_write_pos;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_request",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  Info = GOOD;
  result = result_buffer;
  result_buffer[0] = 0;
  if (!(a->profile.Global_Options & GL_LINE_INTERCONNECT_SUPPORTED))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Facility not supported",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    Info = _FACILITY_NOT_SUPPORTED;
  }
  else if (api_parse (&msg[1].info[1], msg[1].length, "ws", li_parms))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    Info = _WRONG_MESSAGE_FORMAT;
  }
  else
  {
    result_buffer[0] = 3;
    PUT_WORD (&result_buffer[1], GET_WORD (li_parms[0].info));
    result_buffer[3] = 0;
    switch (GET_WORD (li_parms[0].info))
    {
    case LI_GET_SUPPORTED_SERVICES:
      if (appl->appl_flags & APPL_FLAG_OLD_LI_SPEC)
      {
        result_buffer[0] = 17;
        result_buffer[3] = 14;
        PUT_WORD (&result_buffer[4], GOOD);
        d = 0;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_MIXER_CH_CH)
          d |= LI_CONFERENCING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_MIXER_CH_PC)
          d |= LI_MONITORING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_MIXER_PC_CH)
          d |= LI_ANNOUNCEMENTS_SUPPORTED | LI_MIXING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
          d |= LI_CROSS_CONTROLLER_SUPPORTED;
        PUT_DWORD (&result_buffer[6], d);
        if (a->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
        {
          d = 0;
          for (i = 0; i < li_total_channels; i++)
          {
            if ((li_config_table[i].adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
             && (li_config_table[i].adapter->li_pri
              || (i < li_config_table[i].adapter->li_base + MIXER_BCHANNELS_BRI)))
            {
              d++;
            }
          }
        }
        else
        {
          d = a->li_pri ? a->li_channels : MIXER_BCHANNELS_BRI;
        }
        PUT_DWORD (&result_buffer[10], d / 2);
        PUT_DWORD (&result_buffer[14], d);
      }
      else
      {
        result_buffer[0] = 25;
        result_buffer[3] = 22;
        PUT_WORD (&result_buffer[4], GOOD);
        d = LI2_ASYMMETRIC_SUPPORTED | LI2_B_LOOPING_SUPPORTED | LI2_X_LOOPING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_MIXER_CH_PC)
          d |= LI2_MONITORING_SUPPORTED | LI2_REMOTE_MONITORING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_MIXER_PC_CH)
          d |= LI2_MIXING_SUPPORTED | LI2_REMOTE_MIXING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_MIXER_PC_PC)
          d |= LI2_PC_LOOPING_SUPPORTED;
        if (a->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
          d |= LI2_CROSS_CONTROLLER_SUPPORTED;
        PUT_DWORD (&result_buffer[6], d);
        d = a->li_pri ? a->li_channels : MIXER_BCHANNELS_BRI;
        PUT_DWORD (&result_buffer[10], d / 2);
        PUT_DWORD (&result_buffer[14], d - 1);
        if (a->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
        {
          d = 0;
          for (i = 0; i < li_total_channels; i++)
          {
            if ((li_config_table[i].adapter->manufacturer_features & MANUFACTURER_FEATURE_XCONNECT)
             && (li_config_table[i].adapter->li_pri
              || (i < li_config_table[i].adapter->li_base + MIXER_BCHANNELS_BRI)))
            {
              d++;
            }
          }
        }
        PUT_DWORD (&result_buffer[18], d / 2);
        PUT_DWORD (&result_buffer[22], d - 1);
      }
      break;

    case LI_REQ_CONNECT:
      if (li_parms[1].length == 8)
      {
        appl->appl_flags |= APPL_FLAG_OLD_LI_SPEC;
        if (api_parse (&li_parms[1].info[1], li_parms[1].length, "dd", li_req_parms))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_MESSAGE_FORMAT;
          break;
        }
        plci_b_id = GET_DWORD (li_req_parms[0].info) & 0xffff;
        li_flags = GET_DWORD (li_req_parms[1].info);
        Info = li_check_main_plci (Id, plci);
        result_buffer[0] = 9;
        result_buffer[3] = 6;
        PUT_DWORD (&result_buffer[4], plci_b_id);
        PUT_WORD (&result_buffer[8], GOOD);
        if (Info != GOOD)
          break;
        result = plci->saved_msg.info;
        for (i = 0; i <= result_buffer[0]; i++)
          result[i] = result_buffer[i];
        plci_b_write_pos = plci->li_plci_b_write_pos;
        plci_b = li_check_plci_b (Id, plci, plci_b_id, plci_b_write_pos, &result[8]);
        if (plci_b == NULL)
          break;
        li_update_connect (Id, a, plci, plci_b_id, true, li_flags);
        plci->li_plci_b_queue[plci_b_write_pos] = plci_b_id | LI_PLCI_B_LAST_FLAG;
        plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
        plci->li_plci_b_write_pos = plci_b_write_pos;
      }
      else
      {
        appl->appl_flags &= ~APPL_FLAG_OLD_LI_SPEC;
        if (api_parse (&li_parms[1].info[1], li_parms[1].length, "ds", li_req_parms))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_MESSAGE_FORMAT;
          break;
        }
        li_flags = GET_DWORD (li_req_parms[0].info) & ~(LI2_FLAG_INTERCONNECT_A_B | LI2_FLAG_INTERCONNECT_B_A);
        Info = li_check_main_plci (Id, plci);
        result_buffer[0] = 7;
        result_buffer[3] = 4;
        PUT_WORD (&result_buffer[4], Info);
        result_buffer[6] = 0;
        if (Info != GOOD)
          break;
        result = plci->saved_msg.info;
        for (i = 0; i <= result_buffer[0]; i++)
          result[i] = result_buffer[i];
        plci_b_write_pos = plci->li_plci_b_write_pos;
        participant_parms_pos = 0;
        result_pos = 7;
        li2_update_connect (Id, a, plci, UnMapId (Id), true, li_flags);
        while (participant_parms_pos < li_req_parms[1].length)
        {
          result[result_pos] = 6;
          result_pos += 7;
          PUT_DWORD (&result[result_pos - 6], 0);
          PUT_WORD (&result[result_pos - 2], GOOD);
          if (api_parse (&li_req_parms[1].info[1 + participant_parms_pos],
            (word)(li_parms[1].length - participant_parms_pos), "s", li_participant_struct))
          {
            dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
              UnMapId (Id), (char   *)(FILE_), __LINE__));
            PUT_WORD (&result[result_pos - 2], _WRONG_MESSAGE_FORMAT);
            break;
          }
          if (api_parse (&li_participant_struct[0].info[1],
            li_participant_struct[0].length, "dd", li_participant_parms))
          {
            dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
              UnMapId (Id), (char   *)(FILE_), __LINE__));
            PUT_WORD (&result[result_pos - 2], _WRONG_MESSAGE_FORMAT);
            break;
          }
          plci_b_id = GET_DWORD (li_participant_parms[0].info) & 0xffff;
          li_flags = GET_DWORD (li_participant_parms[1].info);
          PUT_DWORD (&result[result_pos - 6], plci_b_id);
          if (sizeof(result) - result_pos < 7)
          {
            dbug (1, dprintf ("[%06lx] %s,%d: LI result overrun",
              UnMapId (Id), (char   *)(FILE_), __LINE__));
            PUT_WORD (&result[result_pos - 2], _WRONG_STATE);
            break;
          }
          plci_b = li2_check_plci_b (Id, plci, plci_b_id, plci_b_write_pos, &result[result_pos - 2]);
          if (plci_b != NULL)
          {
            li2_update_connect (Id, a, plci, plci_b_id, true, li_flags);
            plci->li_plci_b_queue[plci_b_write_pos] = plci_b_id |
              ((li_flags & (LI2_FLAG_INTERCONNECT_A_B | LI2_FLAG_INTERCONNECT_B_A |
              LI2_FLAG_PCCONNECT_A_B | LI2_FLAG_PCCONNECT_B_A)) ? 0 : LI_PLCI_B_DISC_FLAG);
            plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
          }
          participant_parms_pos = (word)((&li_participant_struct[0].info[1 + li_participant_struct[0].length]) -
            (&li_req_parms[1].info[1]));
        }
        result[0] = (byte)(result_pos - 1);
        result[3] = (byte)(result_pos - 4);
        result[6] = (byte)(result_pos - 7);
        i = (plci_b_write_pos == 0) ? LI_PLCI_B_QUEUE_ENTRIES-1 : plci_b_write_pos - 1;
        if ((plci_b_write_pos == plci->li_plci_b_read_pos)
         || (plci->li_plci_b_queue[i] & LI_PLCI_B_LAST_FLAG))
        {
          plci->li_plci_b_queue[plci_b_write_pos] = LI_PLCI_B_SKIP_FLAG | LI_PLCI_B_LAST_FLAG;
          plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
        }
        else
          plci->li_plci_b_queue[i] |= LI_PLCI_B_LAST_FLAG;
        plci->li_plci_b_write_pos = plci_b_write_pos;
      }
      mixer_calculate_coefs (a);
      plci->li_channel_bits = li_config_table[a->li_base + (plci->li_bchannel_id - 1)].channel;
      mixer_notify_update (plci, true);
      sendf (appl, _FACILITY_R | CONFIRM, Id & 0xffffL, Number,
        "wwS", Info, SELECTOR_LINE_INTERCONNECT, result);
      plci->command = 0;
      plci->li_cmd = GET_WORD (li_parms[0].info);
      start_internal_command (Id, plci, mixer_command);
      return (false);

    case LI_REQ_DISCONNECT:
      if (li_parms[1].length == 4)
      {
        appl->appl_flags |= APPL_FLAG_OLD_LI_SPEC;
        if (api_parse (&li_parms[1].info[1], li_parms[1].length, "d", li_req_parms))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_MESSAGE_FORMAT;
          break;
        }
        plci_b_id = GET_DWORD (li_req_parms[0].info) & 0xffff;
        Info = li_check_main_plci (Id, plci);
        result_buffer[0] = 9;
        result_buffer[3] = 6;
        PUT_DWORD (&result_buffer[4], GET_DWORD (li_req_parms[0].info));
        PUT_WORD (&result_buffer[8], GOOD);
        if (Info != GOOD)
          break;
        result = plci->saved_msg.info;
        for (i = 0; i <= result_buffer[0]; i++)
          result[i] = result_buffer[i];
        plci_b_write_pos = plci->li_plci_b_write_pos;
        plci_b = li_check_plci_b (Id, plci, plci_b_id, plci_b_write_pos, &result[8]);
        if (plci_b == NULL)
          break;
        li_update_connect (Id, a, plci, plci_b_id, false, 0);
        plci->li_plci_b_queue[plci_b_write_pos] = plci_b_id | LI_PLCI_B_DISC_FLAG | LI_PLCI_B_LAST_FLAG;
        plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
        plci->li_plci_b_write_pos = plci_b_write_pos;
      }
      else
      {
        appl->appl_flags &= ~APPL_FLAG_OLD_LI_SPEC;
        if (api_parse (&li_parms[1].info[1], li_parms[1].length, "s", li_req_parms))
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_MESSAGE_FORMAT;
          break;
        }
        Info = li_check_main_plci (Id, plci);
        result_buffer[0] = 7;
        result_buffer[3] = 4;
        PUT_WORD (&result_buffer[4], Info);
        result_buffer[6] = 0;
        if (Info != GOOD)
          break;
        result = plci->saved_msg.info;
        for (i = 0; i <= result_buffer[0]; i++)
          result[i] = result_buffer[i];
        plci_b_write_pos = plci->li_plci_b_write_pos;
        participant_parms_pos = 0;
        result_pos = 7;
        while (participant_parms_pos < li_req_parms[0].length)
        {
          result[result_pos] = 6;
          result_pos += 7;
          PUT_DWORD (&result[result_pos - 6], 0);
          PUT_WORD (&result[result_pos - 2], GOOD);
          if (api_parse (&li_req_parms[0].info[1 + participant_parms_pos],
            (word)(li_parms[1].length - participant_parms_pos), "s", li_participant_struct))
          {
            dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
              UnMapId (Id), (char   *)(FILE_), __LINE__));
            PUT_WORD (&result[result_pos - 2], _WRONG_MESSAGE_FORMAT);
            break;
          }
          if (api_parse (&li_participant_struct[0].info[1],
            li_participant_struct[0].length, "d", li_participant_parms))
          {
            dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
              UnMapId (Id), (char   *)(FILE_), __LINE__));
            PUT_WORD (&result[result_pos - 2], _WRONG_MESSAGE_FORMAT);
            break;
          }
          plci_b_id = GET_DWORD (li_participant_parms[0].info) & 0xffff;
          PUT_DWORD (&result[result_pos - 6], plci_b_id);
          if (sizeof(result) - result_pos < 7)
          {
            dbug (1, dprintf ("[%06lx] %s,%d: LI result overrun",
              UnMapId (Id), (char   *)(FILE_), __LINE__));
            PUT_WORD (&result[result_pos - 2], _WRONG_STATE);
            break;
          }
          plci_b = li2_check_plci_b (Id, plci, plci_b_id, plci_b_write_pos, &result[result_pos - 2]);
          if (plci_b != NULL)
          {
            li2_update_connect (Id, a, plci, plci_b_id, false, 0);
            plci->li_plci_b_queue[plci_b_write_pos] = plci_b_id | LI_PLCI_B_DISC_FLAG;
            plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
          }
          participant_parms_pos = (word)((&li_participant_struct[0].info[1 + li_participant_struct[0].length]) -
            (&li_req_parms[0].info[1]));
        }
        result[0] = (byte)(result_pos - 1);
        result[3] = (byte)(result_pos - 4);
        result[6] = (byte)(result_pos - 7);
        i = (plci_b_write_pos == 0) ? LI_PLCI_B_QUEUE_ENTRIES-1 : plci_b_write_pos - 1;
        if ((plci_b_write_pos == plci->li_plci_b_read_pos)
         || (plci->li_plci_b_queue[i] & LI_PLCI_B_LAST_FLAG))
        {
          plci->li_plci_b_queue[plci_b_write_pos] = LI_PLCI_B_SKIP_FLAG | LI_PLCI_B_LAST_FLAG;
          plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
        }
        else
          plci->li_plci_b_queue[i] |= LI_PLCI_B_LAST_FLAG;
        plci->li_plci_b_write_pos = plci_b_write_pos;
      }
      mixer_calculate_coefs (a);
      plci->li_channel_bits = li_config_table[a->li_base + (plci->li_bchannel_id - 1)].channel;
      mixer_notify_update (plci, true);
      sendf (appl, _FACILITY_R | CONFIRM, Id & 0xffffL, Number,
        "wwS", Info, SELECTOR_LINE_INTERCONNECT, result);
      plci->command = 0;
      plci->li_cmd = GET_WORD (li_parms[0].info);
      start_internal_command (Id, plci, mixer_command);
      return (false);

    case LI_REQ_SILENT_UPDATE:
      if (!plci || !plci->State
       || !plci->NL.Id || plci->nl_remove_id
       || (plci->li_bchannel_id == 0)
       || (li_config_table[plci->adapter->li_base + (plci->li_bchannel_id - 1)].plci != plci))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Wrong state",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        return (false);
      }
      plci_b_write_pos = plci->li_plci_b_write_pos;
      if (((plci->li_plci_b_read_pos > plci_b_write_pos) ? plci->li_plci_b_read_pos :
        LI_PLCI_B_QUEUE_ENTRIES + plci->li_plci_b_read_pos) - plci_b_write_pos - 1 < 2)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: LI request overrun",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        return (false);
      }
      i = (plci_b_write_pos == 0) ? LI_PLCI_B_QUEUE_ENTRIES-1 : plci_b_write_pos - 1;
      if ((plci_b_write_pos == plci->li_plci_b_read_pos)
       || (plci->li_plci_b_queue[i] & LI_PLCI_B_LAST_FLAG))
      {
        plci->li_plci_b_queue[plci_b_write_pos] = LI_PLCI_B_SKIP_FLAG | LI_PLCI_B_LAST_FLAG;
        plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
      }
      else
        plci->li_plci_b_queue[i] |= LI_PLCI_B_LAST_FLAG;
      plci->li_plci_b_write_pos = plci_b_write_pos;
      plci->li_channel_bits = li_config_table[a->li_base + (plci->li_bchannel_id - 1)].channel;
      plci->command = 0;
      plci->li_cmd = GET_WORD (li_parms[0].info);
      start_internal_command (Id, plci, mixer_command);
      return (false);

    default:
      dbug (1, dprintf ("[%06lx] %s,%d: LI unknown request %04x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, GET_WORD (li_parms[0].info)));
      Info = _FACILITY_NOT_SUPPORTED;
    }
  }
  sendf (appl, _FACILITY_R | CONFIRM, Id & 0xffffL, Number,
    "wwS", Info, SELECTOR_LINE_INTERCONNECT, result);
  return (false);
}


static void mixer_indication_coefs_set (dword Id, PLCI   *plci)
{
  dword d;
  DIVA_CAPI_ADAPTER   *a;
    byte result[12];

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_indication_coefs_set",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  a = plci->adapter;
  if (plci->li_plci_b_read_pos != plci->li_plci_b_req_pos)
  {
    do
    {
      d = plci->li_plci_b_queue[plci->li_plci_b_read_pos];
      if (!(d & LI_PLCI_B_SKIP_FLAG))
      {
        if (plci->appl->appl_flags & APPL_FLAG_OLD_LI_SPEC)
        {
          if (d & LI_PLCI_B_DISC_FLAG)
          {
            result[0] = 5;
            PUT_WORD (&result[1], LI_IND_DISCONNECT);
            result[3] = 2;
            PUT_WORD (&result[4], _LI_USER_INITIATED);
          }
          else
          {
            result[0] = 7;
            PUT_WORD (&result[1], LI_IND_CONNECT_ACTIVE);
            result[3] = 4;
            PUT_DWORD (&result[4], d & ~LI_PLCI_B_FLAG_MASK);
          }
        }
        else
        {
          if (d & LI_PLCI_B_DISC_FLAG)
          {
            result[0] = 9;
            PUT_WORD (&result[1], LI_IND_DISCONNECT);
            result[3] = 6;
            PUT_DWORD (&result[4], d & ~LI_PLCI_B_FLAG_MASK);
            PUT_WORD (&result[8], _LI_USER_INITIATED);
          }
          else
          {
            result[0] = 7;
            PUT_WORD (&result[1], LI_IND_CONNECT_ACTIVE);
            result[3] = 4;
            PUT_DWORD (&result[4], d & ~LI_PLCI_B_FLAG_MASK);
          }
        }
        sendf (plci->appl, _FACILITY_I, Id & 0xffffL, 0,
          "ws", SELECTOR_LINE_INTERCONNECT, result);
      }
      plci->li_plci_b_read_pos = (plci->li_plci_b_read_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ?
        0 : plci->li_plci_b_read_pos + 1;
    } while (!(d & LI_PLCI_B_LAST_FLAG) && (plci->li_plci_b_read_pos != plci->li_plci_b_req_pos));
  }
}


static void mixer_indication_xconnect_from (dword Id, PLCI   *plci, byte   *msg, word length)
{
  word i, j, ch;
  struct xconnect_transfer_address_s s,   *p;
  DIVA_CAPI_ADAPTER   *a;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_indication_xconnect_from %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, (int) length));

  a = plci->adapter;
  i = 1;
  for (i = 1; i < length; i += 16)
  {
    s.card_address.low = msg[i] | (msg[i+1] << 8) | (((dword)(msg[i+2])) << 16) | (((dword)(msg[i+3])) << 24);
    s.card_address.high = msg[i+4] | (msg[i+5] << 8) | (((dword)(msg[i+6])) << 16) | (((dword)(msg[i+7])) << 24);
    s.offset = msg[i+8] | (msg[i+9] << 8) | (((dword)(msg[i+10])) << 16) | (((dword)(msg[i+11])) << 24);
    ch = msg[i+12] | (msg[i+13] << 8);
    j = ch & XCONNECT_CHANNEL_NUMBER_MASK;
    if (!a->li_pri && (plci->li_bchannel_id == 2))
      j = 1 - j;
    j += a->li_base;
    if (ch & XCONNECT_CHANNEL_PORT_PC)
      p = &(li_config_table[j].send_pc);
    else
      p = &(li_config_table[j].send_b);
    p->card_address.low = s.card_address.low;
    p->card_address.high = s.card_address.high;
    p->offset = s.offset;
    li_config_table[j].channel |= LI_CHANNEL_ADDRESSES_SET;
  }
  if (plci->internal_command_queue[0]
   && ((plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_2)
    || (plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_3)
    || (plci->adjust_b_state == ADJUST_B_RESTORE_MIXER_4)))
  {
    (*(plci->internal_command_queue[0]))(Id, plci, 0);
    if (!plci->internal_command)
      next_internal_command (Id, plci);
  }
  mixer_notify_update (plci, true);
}


static void mixer_indication_xconnect_to (dword Id, PLCI   *plci, byte   *msg, word length)
{

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_indication_xconnect_to %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, (int) length));

}


static byte mixer_notify_source_removed (PLCI   *plci, dword plci_b_id)
{
  word plci_b_write_pos;

  plci_b_write_pos = plci->li_plci_b_write_pos;
  if (((plci->li_plci_b_read_pos > plci_b_write_pos) ? plci->li_plci_b_read_pos :
    LI_PLCI_B_QUEUE_ENTRIES + plci->li_plci_b_read_pos) - plci_b_write_pos - 1 < 1)
  {
    dbug (1, dprintf ("[%06lx] %s,%d: LI request overrun",
      (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
      (char   *)(FILE_), __LINE__));
    return (false);
  }
  plci->li_plci_b_queue[plci_b_write_pos] = plci_b_id | LI_PLCI_B_DISC_FLAG;
  plci_b_write_pos = (plci_b_write_pos == LI_PLCI_B_QUEUE_ENTRIES-1) ? 0 : plci_b_write_pos + 1;
  plci->li_plci_b_write_pos = plci_b_write_pos;
  return (true);
}


static void mixer_remove (PLCI   *plci)
{
  DIVA_CAPI_ADAPTER   *a;
  PLCI   *notify_plci;
  dword plci_b_id;
  word i, j;

  dbug (1, dprintf ("[%06lx] %s,%d: mixer_remove",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  a = plci->adapter;
  plci_b_id = (plci->Id << 8) | UnMapController (plci->adapter->Id);
  if (a->profile.Global_Options & GL_LINE_INTERCONNECT_SUPPORTED)
  {
    if ((plci->li_bchannel_id != 0)
     && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
    {
      i = a->li_base + (plci->li_bchannel_id - 1);
      if ((li_config_table[i].curchnl | li_config_table[i].channel) & LI_CHANNEL_INVOLVED)
      {
        for (j = 0; j < li_total_channels; j++)
        {
          if ((li_config_table[i].flag_table[j] & LI_FLAG_INTERCONNECT)
           || (li_config_table[j].flag_table[i] & LI_FLAG_INTERCONNECT))
          {
            notify_plci = li_config_table[j].plci;
            if ((notify_plci != NULL)
             && (notify_plci != plci)
             && (notify_plci->appl != NULL)
             && !(notify_plci->appl->appl_flags & APPL_FLAG_OLD_LI_SPEC)
             && (notify_plci->State)
             && notify_plci->NL.Id && !notify_plci->nl_remove_id)
            {
              mixer_notify_source_removed (notify_plci, plci_b_id);
            }
          }
        }
        mixer_clear_config (plci);
        mixer_calculate_coefs (a);
        mixer_notify_update (plci, true);
      }
      li_config_table[i].plci = NULL;
      plci->li_bchannel_id = 0;
    }
  }
}


/*------------------------------------------------------------------*/
/* Echo canceller facilities                                        */
/*------------------------------------------------------------------*/


static void ec_write_parameters (PLCI   *plci)
{
  word w;
    byte parameter_buffer[6];

  dbug (1, dprintf ("[%06lx] %s,%d: ec_write_parameters",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  parameter_buffer[0] = 5;
  parameter_buffer[1] = DSP_CTRL_SET_LEC_PARAMETERS;
  PUT_WORD (&parameter_buffer[2], plci->ec_idi_options);
  plci->ec_idi_options &= ~LEC_RESET_COEFFICIENTS;
  w = (plci->ec_tail_length == 0) ? 128 : plci->ec_tail_length;
  PUT_WORD (&parameter_buffer[4], w);
  add_p (plci, FTY, parameter_buffer);
  sig_req (plci, TEL_CTRL, 0);
  send_req (plci);
}


static void ec_clear_config (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: ec_clear_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->ec_idi_options = LEC_ENABLE_ECHO_CANCELLER |
    LEC_MANUAL_DISABLE | LEC_ENABLE_NONLINEAR_PROCESSING;
  plci->ec_tail_length = 0;
}


static void ec_prepare_switch (dword Id, PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: ec_prepare_switch",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

}


static word ec_save_config (dword Id, PLCI   *plci, byte Rc)
{

  dbug (1, dprintf ("[%06lx] %s,%d: ec_save_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  return (GOOD);
}


static word ec_restore_config (dword Id, PLCI   *plci, byte Rc)
{
  word Info;

  dbug (1, dprintf ("[%06lx] %s,%d: ec_restore_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  Info = GOOD;
  if (plci->B1_facilities & B1_FACILITY_EC)
  {
    switch (plci->adjust_b_state)
    {
    case ADJUST_B_RESTORE_EC_1:
      plci->internal_command = plci->adjust_b_command;
      if (plci->sig_req)
      {
        plci->adjust_b_state = ADJUST_B_RESTORE_EC_1;
        break;
      }
      ec_write_parameters (plci);
      plci->adjust_b_state = ADJUST_B_RESTORE_EC_2;
      break;
    case ADJUST_B_RESTORE_EC_2:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Restore EC failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _WRONG_STATE;
        break;
      }
      break;
    }
  }
  return (Info);
}


static void ec_command (dword Id, PLCI   *plci, byte Rc)
{
  word internal_command, Info;
    byte result[8];

  dbug (1, dprintf ("[%06lx] %s,%d: ec_command %02x %04x %04x %04x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command,
    plci->ec_cmd, plci->ec_idi_options, plci->ec_tail_length));

  Info = GOOD;
  if (plci->appl->appl_flags & APPL_FLAG_PRIV_EC_SPEC)
  {
    result[0] = 2;
    PUT_WORD (&result[1], EC_SUCCESS);
  }
  else
  {
    result[0] = 5;
    PUT_WORD (&result[1], plci->ec_cmd);
    result[3] = 2;
    PUT_WORD (&result[4], GOOD);
  }
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (plci->ec_cmd)
  {
  case EC_ENABLE_OPERATION:
  case EC_FREEZE_COEFFICIENTS:
  case EC_RESUME_COEFFICIENT_UPDATE:
  case EC_RESET_COEFFICIENTS:
    switch (internal_command)
    {
    default:
      adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities |
        B1_FACILITY_EC), EC_COMMAND_1);
    case EC_COMMAND_1:
      if (adjust_b_process (Id, plci, Rc) != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Load EC failed",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      if (plci->internal_command)
        return;
    case EC_COMMAND_2:
      if (plci->sig_req)
      {
        plci->internal_command = EC_COMMAND_2;
        return;
      }
      plci->internal_command = EC_COMMAND_3;
      ec_write_parameters (plci);
      return;
    case EC_COMMAND_3:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Enable EC failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      break;
    }
    break;

  case EC_DISABLE_OPERATION:
    switch (internal_command)
    {
    default:
    case EC_COMMAND_1:
      if (plci->B1_facilities & B1_FACILITY_EC)
      {
        if (plci->sig_req)
        {
          plci->internal_command = EC_COMMAND_1;
          return;
        }
        plci->internal_command = EC_COMMAND_2;
        ec_write_parameters (plci);
        return;
      }
      Rc = OK;
    case EC_COMMAND_2:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Disable EC failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      adjust_b1_resource (Id, plci, NULL, (word)(plci->B1_facilities &
        ~B1_FACILITY_EC), EC_COMMAND_3);
    case EC_COMMAND_3:
      if (adjust_b_process (Id, plci, Rc) != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Unload EC failed",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _FACILITY_NOT_SUPPORTED;
        break;
      }
      if (plci->internal_command)
        return;
      break;
    }
    break;
  }
  sendf (plci->appl, _FACILITY_R | CONFIRM, Id & 0xffffL, plci->number,
    "wws", Info, (plci->appl->appl_flags & APPL_FLAG_PRIV_EC_SPEC) ?
    PRIV_SELECTOR_ECHO_CANCELLER : SELECTOR_ECHO_CANCELLER, result);
}


static byte ec_request (dword Id, word Number, DIVA_CAPI_ADAPTER   *a, PLCI   *plci, APPL   *appl, API_PARSE *msg)
{
  word Info;
  word opt;
    API_PARSE ec_parms[3];
    byte result[16];

  dbug (1, dprintf ("[%06lx] %s,%d: ec_request",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  Info = GOOD;
  result[0] = 0;
  if (!(a->man_profile.private_options & (1L << PRIVATE_ECHO_CANCELLER)))
  {
    dbug (1, dprintf ("[%06lx] %s,%d: Facility not supported",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
    Info = _FACILITY_NOT_SUPPORTED;
  }
  else
  {
    if (appl->appl_flags & APPL_FLAG_PRIV_EC_SPEC)
    {
      if (api_parse (&msg[1].info[1], msg[1].length, "w", ec_parms))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _WRONG_MESSAGE_FORMAT;
      }
      else
      {
        if (plci == NULL)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong PLCI",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_IDENTIFIER;
        }
        else if (!plci->State || !plci->NL.Id || plci->nl_remove_id)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong state",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_STATE;
        }
        else
        {
          plci->command = 0;
          plci->ec_cmd = GET_WORD (ec_parms[0].info);
          plci->ec_idi_options &= ~(LEC_MANUAL_DISABLE | LEC_RESET_COEFFICIENTS);
          result[0] = 2;
          PUT_WORD (&result[1], EC_SUCCESS);
          if (msg[1].length >= 4)
          {
            opt = GET_WORD (&ec_parms[0].info[2]);
            plci->ec_idi_options &= ~(LEC_ENABLE_NONLINEAR_PROCESSING |
              LEC_ENABLE_2100HZ_DETECTOR | LEC_REQUIRE_2100HZ_REVERSALS);
            if (!(opt & EC_DISABLE_NON_LINEAR_PROCESSING))
              plci->ec_idi_options |= LEC_ENABLE_NONLINEAR_PROCESSING;
            if (opt & EC_DETECT_DISABLE_TONE)
              plci->ec_idi_options |= LEC_ENABLE_2100HZ_DETECTOR;
            if (!(opt & EC_DO_NOT_REQUIRE_REVERSALS))
              plci->ec_idi_options |= LEC_REQUIRE_2100HZ_REVERSALS;
            if (msg[1].length >= 6)
            {
              plci->ec_tail_length = GET_WORD (&ec_parms[0].info[4]);
            }
          }
          switch (plci->ec_cmd)
          {
          case EC_ENABLE_OPERATION:
            plci->ec_idi_options &= ~LEC_FREEZE_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          case EC_DISABLE_OPERATION:
            plci->ec_idi_options = LEC_ENABLE_ECHO_CANCELLER |
              LEC_MANUAL_DISABLE | LEC_ENABLE_NONLINEAR_PROCESSING |
              LEC_RESET_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          case EC_FREEZE_COEFFICIENTS:
            plci->ec_idi_options |= LEC_FREEZE_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          case EC_RESUME_COEFFICIENT_UPDATE:
            plci->ec_idi_options &= ~LEC_FREEZE_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          case EC_RESET_COEFFICIENTS:
            plci->ec_idi_options |= LEC_RESET_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          default:
            dbug (1, dprintf ("[%06lx] %s,%d: EC unknown request %04x",
              UnMapId (Id), (char   *)(FILE_), __LINE__, plci->ec_cmd));
            PUT_WORD (&result[1], EC_UNSUPPORTED_OPERATION);
          }
        }
      }
    }
    else
    {
      if (api_parse (&msg[1].info[1], msg[1].length, "ws", ec_parms))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Wrong message format",
          UnMapId (Id), (char   *)(FILE_), __LINE__));
        Info = _WRONG_MESSAGE_FORMAT;
      }
      else
      {
        if (GET_WORD (ec_parms[0].info) == EC_GET_SUPPORTED_SERVICES)
        {
          result[0] = 11;
          PUT_WORD (&result[1], EC_GET_SUPPORTED_SERVICES);
          result[3] = 8;
          PUT_WORD (&result[4], GOOD);
          PUT_WORD (&result[6], 0x0007);
          PUT_WORD (&result[8], LEC_MAX_SUPPORTED_TAIL_LENGTH);
          PUT_WORD (&result[10], 0);
        }
        else if (plci == NULL)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong PLCI",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_IDENTIFIER;
        }
        else if (!plci->State || !plci->NL.Id || plci->nl_remove_id)
        {
          dbug (1, dprintf ("[%06lx] %s,%d: Wrong state",
            UnMapId (Id), (char   *)(FILE_), __LINE__));
          Info = _WRONG_STATE;
        }
        else
        {
          plci->command = 0;
          plci->ec_cmd = GET_WORD (ec_parms[0].info);
          plci->ec_idi_options &= ~(LEC_MANUAL_DISABLE | LEC_RESET_COEFFICIENTS);
          result[0] = 5;
          PUT_WORD (&result[1], plci->ec_cmd);
          result[3] = 2;
          PUT_WORD (&result[4], GOOD);
          plci->ec_idi_options &= ~(LEC_ENABLE_NONLINEAR_PROCESSING |
            LEC_ENABLE_2100HZ_DETECTOR | LEC_REQUIRE_2100HZ_REVERSALS);
          plci->ec_tail_length = 0;
          if (ec_parms[1].length >= 2)
          {
            opt = GET_WORD (&ec_parms[1].info[1]);
            if (opt & EC_ENABLE_NON_LINEAR_PROCESSING)
              plci->ec_idi_options |= LEC_ENABLE_NONLINEAR_PROCESSING;
            if (opt & EC_DETECT_DISABLE_TONE)
              plci->ec_idi_options |= LEC_ENABLE_2100HZ_DETECTOR;
            if (!(opt & EC_DO_NOT_REQUIRE_REVERSALS))
              plci->ec_idi_options |= LEC_REQUIRE_2100HZ_REVERSALS;
            if (ec_parms[1].length >= 4)
            {
              plci->ec_tail_length = GET_WORD (&ec_parms[1].info[3]);
            }
          }
          switch (plci->ec_cmd)
          {
          case EC_ENABLE_OPERATION:
            plci->ec_idi_options &= ~LEC_FREEZE_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          case EC_DISABLE_OPERATION:
            plci->ec_idi_options = LEC_ENABLE_ECHO_CANCELLER |
              LEC_MANUAL_DISABLE | LEC_ENABLE_NONLINEAR_PROCESSING |
              LEC_RESET_COEFFICIENTS;
            start_internal_command (Id, plci, ec_command);
            return (false);

          default:
            dbug (1, dprintf ("[%06lx] %s,%d: EC unknown request %04x",
              UnMapId (Id), (char   *)(FILE_), __LINE__, plci->ec_cmd));
            PUT_WORD (&result[4], _FACILITY_SPECIFIC_FUNCTION_NOT_SUPP);
          }
        }
      }
    }
  }
  sendf (appl, _FACILITY_R | CONFIRM, Id & 0xffffL, Number,
    "wws", Info, (appl->appl_flags & APPL_FLAG_PRIV_EC_SPEC) ?
    PRIV_SELECTOR_ECHO_CANCELLER : SELECTOR_ECHO_CANCELLER, result);
  return (false);
}


static void ec_indication (dword Id, PLCI   *plci, byte   *msg, word length)
{
    byte result[8];

  dbug (1, dprintf ("[%06lx] %s,%d: ec_indication",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

  if (!(plci->ec_idi_options & LEC_MANUAL_DISABLE))
  {
    if (plci->appl->appl_flags & APPL_FLAG_PRIV_EC_SPEC)
    {
      result[0] = 2;
      PUT_WORD (&result[1], 0);
      switch (msg[1])
      {
      case LEC_DISABLE_TYPE_CONTIGNUOUS_2100HZ:
        PUT_WORD (&result[1], EC_BYPASS_DUE_TO_CONTINUOUS_2100HZ);
        break;
      case LEC_DISABLE_TYPE_REVERSED_2100HZ:
        PUT_WORD (&result[1], EC_BYPASS_DUE_TO_REVERSED_2100HZ);
        break;
      case LEC_DISABLE_RELEASED:
        PUT_WORD (&result[1], EC_BYPASS_RELEASED);
        break;
      }
    }
    else
    {
      result[0] = 5;
      PUT_WORD (&result[1], EC_BYPASS_INDICATION);
      result[3] = 2;
      PUT_WORD (&result[4], 0);
      switch (msg[1])
      {
      case LEC_DISABLE_TYPE_CONTIGNUOUS_2100HZ:
        PUT_WORD (&result[4], EC_BYPASS_DUE_TO_CONTINUOUS_2100HZ);
        break;
      case LEC_DISABLE_TYPE_REVERSED_2100HZ:
        PUT_WORD (&result[4], EC_BYPASS_DUE_TO_REVERSED_2100HZ);
        break;
      case LEC_DISABLE_RELEASED:
        PUT_WORD (&result[4], EC_BYPASS_RELEASED);
        break;
      }
    }
    sendf (plci->appl, _FACILITY_I, Id & 0xffffL, 0, "ws", (plci->appl->appl_flags & APPL_FLAG_PRIV_EC_SPEC) ?
      PRIV_SELECTOR_ECHO_CANCELLER : SELECTOR_ECHO_CANCELLER, result);
  }
}



/*------------------------------------------------------------------*/
/* Advanced voice                                                   */
/*------------------------------------------------------------------*/

static void adv_voice_write_coefs (PLCI   *plci, word write_command)
{
  DIVA_CAPI_ADAPTER   *a;
  word i;
  byte *p;

  word w, n, j, k;
  byte ch_map[MIXER_CHANNELS_BRI];

    byte coef_buffer[ADV_VOICE_COEF_BUFFER_SIZE + 2];

  dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_write_coefs %d",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, write_command));

  a = plci->adapter;
  p = coef_buffer + 1;
  *(p++) = DSP_CTRL_OLD_SET_MIXER_COEFFICIENTS;
  i = 0;
  while (i + sizeof(word) <= a->adv_voice_coef_length)
  {
    PUT_WORD (p, GET_WORD (a->adv_voice_coef_buffer + i));
    p += 2;
    i += 2;
  }
  while (i < ADV_VOICE_OLD_COEF_COUNT * sizeof(word))
  {
    PUT_WORD (p, 0x8000);
    p += 2;
    i += 2;
  }

  if (!a->li_pri && (plci->li_bchannel_id == 0))
  {
    if ((li_config_table[a->li_base].plci == NULL) && (li_config_table[a->li_base + 1].plci != NULL))
    {
      plci->li_bchannel_id = 1;
      li_config_table[a->li_base].plci = plci;
      dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_set_bchannel_id %d",
        (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
        (char   *)(FILE_), __LINE__, plci->li_bchannel_id));
    }
    else if ((li_config_table[a->li_base].plci != NULL) && (li_config_table[a->li_base + 1].plci == NULL))
    {
      plci->li_bchannel_id = 2;
      li_config_table[a->li_base + 1].plci = plci;
      dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_set_bchannel_id %d",
        (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
        (char   *)(FILE_), __LINE__, plci->li_bchannel_id));
    }
  }
  if (!a->li_pri && (plci->li_bchannel_id != 0)
   && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    i = a->li_base + (plci->li_bchannel_id - 1);
    switch (write_command)
    {
    case ADV_VOICE_WRITE_ACTIVATION:
      j = a->li_base + MIXER_IC_CHANNEL_BASE + (plci->li_bchannel_id - 1);
      k = a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id);
      if (!(plci->B1_facilities & B1_FACILITY_MIXER))
      {
        li_config_table[j].flag_table[i] |= LI_FLAG_CONFERENCE | LI_FLAG_MIX;
        li_config_table[i].flag_table[j] |= LI_FLAG_CONFERENCE | LI_FLAG_MONITOR;
      }
      if (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC)
      {
        li_config_table[k].flag_table[i] |= LI_FLAG_CONFERENCE | LI_FLAG_MIX;
        li_config_table[i].flag_table[k] |= LI_FLAG_CONFERENCE | LI_FLAG_MONITOR;
        li_config_table[k].flag_table[j] |= LI_FLAG_CONFERENCE;
        li_config_table[j].flag_table[k] |= LI_FLAG_CONFERENCE;
      }
      mixer_calculate_coefs (a);
      li_config_table[i].curchnl = li_config_table[i].channel;
      li_config_table[j].curchnl = li_config_table[j].channel;
      if (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC)
        li_config_table[k].curchnl = li_config_table[k].channel;
      break;

    case ADV_VOICE_WRITE_DEACTIVATION:
      for (j = 0; j < li_total_channels; j++)
      {
        li_config_table[i].flag_table[j] = 0;
        li_config_table[j].flag_table[i] = 0;
      }
      k = a->li_base + MIXER_IC_CHANNEL_BASE + (plci->li_bchannel_id - 1);
      for (j = 0; j < li_total_channels; j++)
      {
        li_config_table[k].flag_table[j] = 0;
        li_config_table[j].flag_table[k] = 0;
      }
      if (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC)
      {
        k = a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id);
        for (j = 0; j < li_total_channels; j++)
        {
          li_config_table[k].flag_table[j] = 0;
          li_config_table[j].flag_table[k] = 0;
        }
      }
      mixer_calculate_coefs (a);
      break;
    }
    if (plci->B1_facilities & B1_FACILITY_MIXER)
    {
      w = 0;
      if (ADV_VOICE_NEW_COEF_BASE + sizeof(word) <= a->adv_voice_coef_length)
        w = GET_WORD (a->adv_voice_coef_buffer + ADV_VOICE_NEW_COEF_BASE);
      if (li_config_table[i].channel & LI_CHANNEL_TX_DATA)
        w |= MIXER_FEATURE_ENABLE_TX_DATA;
      if (li_config_table[i].channel & LI_CHANNEL_RX_DATA)
        w |= MIXER_FEATURE_ENABLE_RX_DATA;
      *(p++) = (byte) w;
      *(p++) = (byte)(w >> 8);
      for (j = 0; j < sizeof(ch_map); j += 2)
      {
        ch_map[j] = (byte)(j + (plci->li_bchannel_id - 1));
        ch_map[j+1] = (byte)(j + (2 - plci->li_bchannel_id));
      }
      for (n = 0; n < ARRAY_SIZE(mixer_write_prog_bri); n++)
      {
        i = a->li_base + ch_map[mixer_write_prog_bri[n].to_ch];
        j = a->li_base + ch_map[mixer_write_prog_bri[n].from_ch];
        if (li_config_table[i].channel & li_config_table[j].channel & LI_CHANNEL_INVOLVED)
        {
          *(p++) = ((li_config_table[i].coef_table[j] & mixer_write_prog_bri[n].mask) ? 0x80 : 0x01);
          w = ((li_config_table[i].coef_table[j] & 0xf) ^ (li_config_table[i].coef_table[j] >> 4));
          li_config_table[i].coef_table[j] ^= (w & mixer_write_prog_bri[n].mask) << 4;
        }
        else
        {
          *(p++) = (ADV_VOICE_NEW_COEF_BASE + sizeof(word) + n < a->adv_voice_coef_length) ?
            a->adv_voice_coef_buffer[ADV_VOICE_NEW_COEF_BASE + sizeof(word) + n] : 0x00;
        }
      }
    }
    else
    {
      for (i = ADV_VOICE_NEW_COEF_BASE; i < a->adv_voice_coef_length; i++)
        *(p++) = a->adv_voice_coef_buffer[i];
    }
  }
  else

  {
    for (i = ADV_VOICE_NEW_COEF_BASE; i < a->adv_voice_coef_length; i++)
      *(p++) = a->adv_voice_coef_buffer[i];
  }
  coef_buffer[0] = (p - coef_buffer) - 1;
  add_p (plci, FTY, coef_buffer);
  sig_req (plci, TEL_CTRL, 0);
  send_req (plci);
}


static void adv_voice_clear_config (PLCI   *plci)
{
  DIVA_CAPI_ADAPTER   *a;

  word i, j;


  dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_clear_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  a = plci->adapter;
  if ((plci->tel == ADV_VOICE) && (plci == a->AdvSignalPLCI))
  {
    a->adv_voice_coef_length = 0;

    if (!a->li_pri && (plci->li_bchannel_id != 0)
     && (li_config_table[a->li_base + (plci->li_bchannel_id - 1)].plci == plci))
    {
      i = a->li_base + (plci->li_bchannel_id - 1);
      li_config_table[i].curchnl = 0;
      li_config_table[i].channel = 0;
      li_config_table[i].chflags = 0;
      for (j = 0; j < li_total_channels; j++)
      {
        li_config_table[i].flag_table[j] = 0;
        li_config_table[j].flag_table[i] = 0;
        li_config_table[i].coef_table[j] = 0;
        li_config_table[j].coef_table[i] = 0;
      }
      li_config_table[i].coef_table[i] |= LI_COEF_CH_PC_SET | LI_COEF_PC_CH_SET;
      i = a->li_base + MIXER_IC_CHANNEL_BASE + (plci->li_bchannel_id - 1);
      li_config_table[i].curchnl = 0;
      li_config_table[i].channel = 0;
      li_config_table[i].chflags = 0;
      for (j = 0; j < li_total_channels; j++)
      {
        li_config_table[i].flag_table[j] = 0;
        li_config_table[j].flag_table[i] = 0;
        li_config_table[i].coef_table[j] = 0;
        li_config_table[j].coef_table[i] = 0;
      }
      if (a->manufacturer_features & MANUFACTURER_FEATURE_SLAVE_CODEC)
      {
        i = a->li_base + MIXER_IC_CHANNEL_BASE + (2 - plci->li_bchannel_id);
        li_config_table[i].curchnl = 0;
        li_config_table[i].channel = 0;
        li_config_table[i].chflags = 0;
        for (j = 0; j < li_total_channels; j++)
        {
          li_config_table[i].flag_table[j] = 0;
          li_config_table[j].flag_table[i] = 0;
          li_config_table[i].coef_table[j] = 0;
          li_config_table[j].coef_table[i] = 0;
        }
      }
    }

  }
}


static void adv_voice_prepare_switch (dword Id, PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_prepare_switch",
    UnMapId (Id), (char   *)(FILE_), __LINE__));

}


static word adv_voice_save_config (dword Id, PLCI   *plci, byte Rc)
{

  dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_save_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  return (GOOD);
}


static word adv_voice_restore_config (dword Id, PLCI   *plci, byte Rc)
{
  DIVA_CAPI_ADAPTER   *a;
  word Info;

  dbug (1, dprintf ("[%06lx] %s,%d: adv_voice_restore_config %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  Info = GOOD;
  a = plci->adapter;
  if ((plci->B1_facilities & B1_FACILITY_VOICE)
   && (plci->tel == ADV_VOICE) && (plci == a->AdvSignalPLCI))
  {
    switch (plci->adjust_b_state)
    {
    case ADJUST_B_RESTORE_VOICE_1:
      plci->internal_command = plci->adjust_b_command;
      if (plci->sig_req)
      {
        plci->adjust_b_state = ADJUST_B_RESTORE_VOICE_1;
        break;
      }
      adv_voice_write_coefs (plci, ADV_VOICE_WRITE_UPDATE);
      plci->adjust_b_state = ADJUST_B_RESTORE_VOICE_2;
      break;
    case ADJUST_B_RESTORE_VOICE_2:
      if ((Rc != OK) && (Rc != OK_FC))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Restore voice config failed %02x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
        Info = _WRONG_STATE;
        break;
      }
      break;
    }
  }
  return (Info);
}




/*------------------------------------------------------------------*/
/* B1 resource switching                                            */
/*------------------------------------------------------------------*/

static byte b1_facilities_table[] =
{
  0x00,  /* 0  No bchannel resources      */
  0x00,  /* 1  Codec (automatic law)      */
  0x00,  /* 2  Codec (A-law)              */
  0x00,  /* 3  Codec (y-law)              */
  0x00,  /* 4  HDLC for X.21              */
  0x00,  /* 5  HDLC                       */
  0x00,  /* 6  External Device 0          */
  0x00,  /* 7  External Device 1          */
  0x00,  /* 8  HDLC 56k                   */
  0x00,  /* 9  Transparent                */
  0x00,  /* 10 Loopback to network        */
  0x00,  /* 11 Test pattern to net        */
  0x00,  /* 12 Rate adaptation sync       */
  0x00,  /* 13 Rate adaptation async      */
  0x00,  /* 14 R-Interface                */
  0x00,  /* 15 HDLC 128k leased line      */
  0x00,  /* 16 FAX                        */
  0x00,  /* 17 Modem async                */
  0x00,  /* 18 Modem sync HDLC            */
  0x00,  /* 19 V.110 async HDLC           */
  0x12,  /* 20 Adv voice (Trans,mixer)    */
  0x00,  /* 21 Codec connected to IC      */
  0x0c,  /* 22 Trans,DTMF                 */
  0x1e,  /* 23 Trans,DTMF+mixer           */
  0x1f,  /* 24 Trans,DTMF+mixer+local     */
  0x13,  /* 25 Trans,mixer+local          */
  0x12,  /* 26 HDLC,mixer                 */
  0x12,  /* 27 HDLC 56k,mixer             */
  0x2c,  /* 28 Trans,LEC+DTMF             */
  0x3e,  /* 29 Trans,LEC+DTMF+mixer       */
  0x3f,  /* 30 Trans,LEC+DTMF+mixer+local */
  0x2c,  /* 31 RTP,LEC+DTMF               */
  0x3e,  /* 32 RTP,LEC+DTMF+mixer         */
  0x3f,  /* 33 RTP,LEC+DTMF+mixer+local   */
  0x00,  /* 34 Signaling task             */
  0x00,  /* 35 PIAFS                      */
  0x0c,  /* 36 Trans,DTMF+TONE            */
  0x1e,  /* 37 Trans,DTMF+TONE+mixer      */
  0x1f   /* 38 Trans,DTMF+TONE+mixer+local*/
};


static word get_b1_facilities (PLCI   * plci, byte b1_resource)
{
  word b1_facilities;

  b1_facilities = b1_facilities_table[b1_resource];
  if ((b1_resource == 9) || (b1_resource == 20) || (b1_resource == 25))
  {

    if (!(((plci->requested_options_conn | plci->requested_options) & (1L << PRIVATE_DTMF_TONE))
       || (plci->appl && (plci->adapter->requested_options_table[plci->appl->Id-1] & (1L << PRIVATE_DTMF_TONE)))))

    {
      if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_SOFTDTMF_SEND)
        b1_facilities |= B1_FACILITY_DTMFX;
      if (plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_SOFTDTMF_RECEIVE)
        b1_facilities |= B1_FACILITY_DTMFR;
    }
  }
  if ((b1_resource == 17) || (b1_resource == 18))
  {
    if (plci->adapter->manufacturer_features & (MANUFACTURER_FEATURE_V18 | MANUFACTURER_FEATURE_VOWN))
      b1_facilities |= B1_FACILITY_DTMFX | B1_FACILITY_DTMFR;
  }
/*
  dbug (1, dprintf ("[%06lx] %s,%d: get_b1_facilities %d %04x",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char far *)(FILE_), __LINE__, b1_resource, b1_facilites));
*/
  return (b1_facilities);
}


static byte add_b1_facilities (PLCI   * plci, byte b1_resource, word b1_facilities)
{
  byte b;

  switch (b1_resource)
  {
  case 5:
  case 26:
    if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
      b = 26;
    else
      b = 5;
    break;

  case 8:
  case 27:
    if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
      b = 27;
    else
      b = 8;
    break;

  case 9:
  case 20:
  case 22:
  case 23:
  case 24:
  case 25:
  case 28:
  case 29:
  case 30:
  case 36:
  case 37:
  case 38:
    if (b1_facilities & B1_FACILITY_EC)
    {
      if (b1_facilities & B1_FACILITY_LOCAL)
        b = 30;
      else if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
        b = 29;
      else
        b = 28;
    }

    else if ((b1_facilities & (B1_FACILITY_DTMFX | B1_FACILITY_DTMFR | B1_FACILITY_MIXER))
      && (((plci->requested_options_conn | plci->requested_options) & (1L << PRIVATE_DTMF_TONE))
       || (plci->appl && (plci->adapter->requested_options_table[plci->appl->Id-1] & (1L << PRIVATE_DTMF_TONE)))))
    {
      if (b1_facilities & B1_FACILITY_LOCAL)
        b = 38;
      else if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
        b = 37;
      else
        b = 36;
    }

    else if (((plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_HARDDTMF)
      && !(plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_SOFTDTMF_RECEIVE))
     || ((b1_facilities & B1_FACILITY_DTMFR)
      && ((b1_facilities & B1_FACILITY_MIXER)
       || !(plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_SOFTDTMF_RECEIVE)))
     || ((b1_facilities & B1_FACILITY_DTMFX)
      && ((b1_facilities & B1_FACILITY_MIXER)
       || !(plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_SOFTDTMF_SEND))))
    {
      if (b1_facilities & B1_FACILITY_LOCAL)
        b = 24;
      else if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
        b = 23;
      else
        b = 22;
    }
    else
    {
      if (b1_facilities & B1_FACILITY_LOCAL)
        b = 25;
      else if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
        b = 20;
      else
        b = 9;
    }
    break;

  case 31:
  case 32:
  case 33:
    if (b1_facilities & B1_FACILITY_LOCAL)
      b = 33;
    else if (b1_facilities & (B1_FACILITY_MIXER | B1_FACILITY_VOICE))
      b = 32;
    else
      b = 31;
    break;

  default:
    b = b1_resource;
  }
  dbug (1, dprintf ("[%06lx] %s,%d: add_b1_facilities %d %04x %d %04x",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__,
    b1_resource, b1_facilities, b, get_b1_facilities (plci, b)));
  return (b);
}


static void adjust_b1_facilities (PLCI   *plci, byte new_b1_resource, word new_b1_facilities)
{
  word removed_facilities;

  dbug (1, dprintf ("[%06lx] %s,%d: adjust_b1_facilities %d %04x %04x",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__, new_b1_resource, new_b1_facilities,
    new_b1_facilities & get_b1_facilities (plci, new_b1_resource)));

  new_b1_facilities &= get_b1_facilities (plci, new_b1_resource);
  removed_facilities = plci->B1_facilities & ~new_b1_facilities;

  if (removed_facilities & B1_FACILITY_EC)
    ec_clear_config (plci);


  if (removed_facilities & B1_FACILITY_DTMFR)
  {
    dtmf_rec_clear_config (plci);
    dtmf_parameter_clear_config (plci);
  }
  if (removed_facilities & B1_FACILITY_DTMFX)
    dtmf_send_clear_config (plci);


  if (removed_facilities & B1_FACILITY_MIXER)
    mixer_clear_config (plci);

  if (removed_facilities & B1_FACILITY_VOICE)
    adv_voice_clear_config (plci);
  plci->B1_facilities = new_b1_facilities;
}


static void adjust_b_clear (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: adjust_b_clear",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->adjust_b_restore = false;
}


static word adjust_b_process (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  byte b1_resource;
  NCCI   * ncci_ptr;
    API_PARSE bp[2];

  dbug (1, dprintf ("[%06lx] %s,%d: adjust_b_process %02x %d",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->adjust_b_state));

  Info = GOOD;
  switch (plci->adjust_b_state)
  {
  case ADJUST_B_START:
    if ((plci->adjust_b_parms_msg == NULL)
     && (plci->adjust_b_mode & ADJUST_B_MODE_SWITCH_L1)
     && ((plci->adjust_b_mode & ~(ADJUST_B_MODE_SAVE | ADJUST_B_MODE_SWITCH_L1 |
      ADJUST_B_MODE_NO_RESOURCE | ADJUST_B_MODE_RESTORE)) == 0))
    {
      b1_resource = (plci->adjust_b_mode == ADJUST_B_MODE_NO_RESOURCE) ?
        0 : add_b1_facilities (plci, plci->B1_resource, plci->adjust_b_facilities);
      if (b1_resource == plci->B1_resource)
      {
        adjust_b1_facilities (plci, b1_resource, plci->adjust_b_facilities);
        break;
      }
      if (plci->adjust_b_facilities & ~get_b1_facilities (plci, b1_resource))
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Adjust B nonsupported facilities %d %d %04x",
          UnMapId (Id), (char   *)(FILE_), __LINE__,
          plci->B1_resource, b1_resource, plci->adjust_b_facilities));
        Info = _WRONG_STATE;
        break;
      }
    }
    if (plci->adjust_b_mode & ADJUST_B_MODE_SAVE)
    {

      mixer_prepare_switch (Id, plci);


      dtmf_prepare_switch (Id, plci);
      dtmf_parameter_prepare_switch (Id, plci);


      ec_prepare_switch (Id, plci);

      adv_voice_prepare_switch (Id, plci);
    }
    plci->adjust_b_state = ADJUST_B_SAVE_MIXER_1;
    Rc = OK;
  case ADJUST_B_SAVE_MIXER_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_SAVE)
    {

      Info = mixer_save_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_SAVE_DTMF_1;
    Rc = OK;
  case ADJUST_B_SAVE_DTMF_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_SAVE)
    {

      Info = dtmf_save_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_REMOVE_L23_1;
  case ADJUST_B_REMOVE_L23_1:
    if ((plci->adjust_b_mode & ADJUST_B_MODE_REMOVE_L23)
     && plci->NL.Id && !plci->nl_remove_id)
    {
      plci->internal_command = plci->adjust_b_command;
      if (plci->adjust_b_ncci != 0)
      {
        ncci_ptr = &(plci->adapter->ncci[plci->adjust_b_ncci]);
        while (ncci_ptr->data_pending)
        {
          plci->data_sent_ptr = ncci_ptr->DBuffer[ncci_ptr->data_out].P;
          data_rc (plci, plci->adapter->ncci_ch[plci->adjust_b_ncci]);
        }
        while (ncci_ptr->data_ack_pending)
          data_ack (plci, plci->adapter->ncci_ch[plci->adjust_b_ncci]);
      }
      nl_req_ncci (plci, REMOVE,
        (byte)((plci->adjust_b_mode & ADJUST_B_MODE_CONNECT) ? plci->adjust_b_ncci : 0));
      send_req (plci);
      plci->adjust_b_state = ADJUST_B_REMOVE_L23_2;
      break;
    }
    plci->adjust_b_state = ADJUST_B_REMOVE_L23_2;
    Rc = OK;
  case ADJUST_B_REMOVE_L23_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Adjust B remove failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      Info = _WRONG_STATE;
      break;
    }
    if (plci->adjust_b_mode & ADJUST_B_MODE_REMOVE_L23)
    {
      if (plci_nl_busy (plci))
      {
        plci->internal_command = plci->adjust_b_command;
        break;
      }
    }
    plci->adjust_b_state = ADJUST_B_SAVE_EC_1;
    Rc = OK;
  case ADJUST_B_SAVE_EC_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_SAVE)
    {

      Info = ec_save_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_SAVE_DTMF_PARAMETER_1;
    Rc = OK;
  case ADJUST_B_SAVE_DTMF_PARAMETER_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_SAVE)
    {

      Info = dtmf_parameter_save_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_SAVE_VOICE_1;
    Rc = OK;
  case ADJUST_B_SAVE_VOICE_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_SAVE)
    {
      Info = adv_voice_save_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;
    }
    plci->adjust_b_state = ADJUST_B_SWITCH_L1_1;
  case ADJUST_B_SWITCH_L1_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_SWITCH_L1)
    {
      if (plci->sig_req)
      {
        plci->internal_command = plci->adjust_b_command;
        break;
      }
      if (plci->adjust_b_parms_msg != NULL)
        api_load_msg (plci->adjust_b_parms_msg, bp);
      else
        api_load_msg (&plci->B_protocol, bp);
      Info = add_b1 (plci, bp,
        (word)((plci->adjust_b_mode & ADJUST_B_MODE_NO_RESOURCE) ? 2 : 0),
        plci->adjust_b_facilities);
      if (Info != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Adjust B invalid L1 parameters %d %04x",
          UnMapId (Id), (char   *)(FILE_), __LINE__,
          plci->B1_resource, plci->adjust_b_facilities));
        break;
      }
      plci->internal_command = plci->adjust_b_command;
      sig_req (plci, RESOURCES, 0);
      send_req (plci);
      plci->adjust_b_state = ADJUST_B_SWITCH_L1_2;
      break;
    }
    plci->adjust_b_state = ADJUST_B_SWITCH_L1_2;
    Rc = OK;
  case ADJUST_B_SWITCH_L1_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Adjust B switch failed %02x %d %04x",
        UnMapId (Id), (char   *)(FILE_), __LINE__,
        Rc, plci->B1_resource, plci->adjust_b_facilities));
      Info = _WRONG_STATE;
      break;
    }
    plci->adjust_b_state = ADJUST_B_RESTORE_VOICE_1;
    Rc = OK;
  case ADJUST_B_RESTORE_VOICE_1:
  case ADJUST_B_RESTORE_VOICE_2:
    if (plci->adjust_b_mode & ADJUST_B_MODE_RESTORE)
    {
      Info = adv_voice_restore_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;
    }
    plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_PARAMETER_1;
    Rc = OK;
  case ADJUST_B_RESTORE_DTMF_PARAMETER_1:
  case ADJUST_B_RESTORE_DTMF_PARAMETER_2:
    if (plci->adjust_b_mode & ADJUST_B_MODE_RESTORE)
    {

      Info = dtmf_parameter_restore_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_RESTORE_EC_1;
    Rc = OK;
  case ADJUST_B_RESTORE_EC_1:
  case ADJUST_B_RESTORE_EC_2:
    if (plci->adjust_b_mode & ADJUST_B_MODE_RESTORE)
    {

      Info = ec_restore_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_ASSIGN_L23_1;
  case ADJUST_B_ASSIGN_L23_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_ASSIGN_L23)
    {
      if (plci_nl_busy (plci))
      {
        plci->internal_command = plci->adjust_b_command;
        break;
      }
      if (plci->adjust_b_mode & ADJUST_B_MODE_CONNECT)
        plci->call_dir |= CALL_DIR_FORCE_OUTG_NL;
      if (plci->adjust_b_parms_msg != NULL)
        api_load_msg (plci->adjust_b_parms_msg, bp);
      else
        api_load_msg (&plci->B_protocol, bp);
      Info = add_b23 (plci, bp);
      if (Info != GOOD)
      {
        dbug (1, dprintf ("[%06lx] %s,%d: Adjust B invalid L23 parameters %04x",
          UnMapId (Id), (char   *)(FILE_), __LINE__, Info));
        break;
      }
      plci->internal_command = plci->adjust_b_command;
      nl_req_ncci (plci, ASSIGN, 0);
      send_req (plci);
      plci->adjust_b_state = ADJUST_B_ASSIGN_L23_2;
      break;
    }
    plci->adjust_b_state = ADJUST_B_ASSIGN_L23_2;
    Rc = ASSIGN_OK;
  case ADJUST_B_ASSIGN_L23_2:
    if ((Rc != OK) && (Rc != OK_FC) && (Rc != ASSIGN_OK))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Adjust B assign failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      Info = _WRONG_STATE;
      break;
    }
    if (plci->adjust_b_mode & ADJUST_B_MODE_ASSIGN_L23)
    {
      if (Rc != ASSIGN_OK)
      {
        plci->internal_command = plci->adjust_b_command;
        break;
      }
    }
    if (plci->adjust_b_mode & ADJUST_B_MODE_USER_CONNECT)
    {
      plci->adjust_b_restore = true;
      break;
    }
    plci->adjust_b_state = ADJUST_B_CONNECT_1;
  case ADJUST_B_CONNECT_1:
    if (plci->adjust_b_mode & ADJUST_B_MODE_CONNECT)
    {
      plci->internal_command = plci->adjust_b_command;
      if (plci_nl_busy (plci))
        break;
      nl_req_ncci (plci, N_CONNECT, 0);
      send_req (plci);
      plci->adjust_b_state = ADJUST_B_CONNECT_2;
      break;
    }
    plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_1;
    Rc = OK;
  case ADJUST_B_CONNECT_2:
  case ADJUST_B_CONNECT_3:
  case ADJUST_B_CONNECT_4:
    if ((Rc != OK) && (Rc != OK_FC) && (Rc != 0))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Adjust B connect failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      Info = _WRONG_STATE;
      break;
    }
    if (Rc == OK)
    {
      if (plci->adjust_b_mode & ADJUST_B_MODE_CONNECT)
      {
        get_ncci (plci, (byte)(Id >> 16), plci->adjust_b_ncci);
        Id = (Id & 0xffff) | (((dword)(plci->adjust_b_ncci)) << 16);
      }
      if (plci->adjust_b_state == ADJUST_B_CONNECT_2)
        plci->adjust_b_state = ADJUST_B_CONNECT_3;
      else if (plci->adjust_b_state == ADJUST_B_CONNECT_4)
        plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_1;
    }
    else if (Rc == 0)
    {
      if (plci->adjust_b_state == ADJUST_B_CONNECT_2)
        plci->adjust_b_state = ADJUST_B_CONNECT_4;
      else if (plci->adjust_b_state == ADJUST_B_CONNECT_3)
        plci->adjust_b_state = ADJUST_B_RESTORE_DTMF_1;
    }
    if (plci->adjust_b_state != ADJUST_B_RESTORE_DTMF_1)
    {
      plci->internal_command = plci->adjust_b_command;
      break;
    }
    Rc = OK;
  case ADJUST_B_RESTORE_DTMF_1:
  case ADJUST_B_RESTORE_DTMF_2:
    if (plci->adjust_b_mode & ADJUST_B_MODE_RESTORE)
    {

      Info = dtmf_restore_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_RESTORE_MIXER_1;
    Rc = OK;
  case ADJUST_B_RESTORE_MIXER_1:
  case ADJUST_B_RESTORE_MIXER_2:
  case ADJUST_B_RESTORE_MIXER_3:
  case ADJUST_B_RESTORE_MIXER_4:
  case ADJUST_B_RESTORE_MIXER_5:
  case ADJUST_B_RESTORE_MIXER_6:
  case ADJUST_B_RESTORE_MIXER_7:
    if (plci->adjust_b_mode & ADJUST_B_MODE_RESTORE)
    {

      Info = mixer_restore_config (Id, plci, Rc);
      if ((Info != GOOD) || plci->internal_command)
        break;

    }
    plci->adjust_b_state = ADJUST_B_END;
  case ADJUST_B_END:
    break;
  }
  return (Info);
}


static void adjust_b1_resource (dword Id, PLCI   *plci, API_SAVE   *bp_msg, word b1_facilities, word internal_command)
{

  dbug (1, dprintf ("[%06lx] %s,%d: adjust_b1_resource %d %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__,
    plci->B1_resource, b1_facilities));

  plci->adjust_b_parms_msg = bp_msg;
  plci->adjust_b_facilities = b1_facilities;
  plci->adjust_b_command = internal_command;
  plci->adjust_b_ncci = (word)(Id >> 16);
  if ((bp_msg == NULL) && (plci->B1_resource == 0))
    plci->adjust_b_mode = ADJUST_B_MODE_SAVE | ADJUST_B_MODE_NO_RESOURCE | ADJUST_B_MODE_SWITCH_L1;
  else
    plci->adjust_b_mode = ADJUST_B_MODE_SAVE | ADJUST_B_MODE_SWITCH_L1 | ADJUST_B_MODE_RESTORE;
  plci->adjust_b_state = ADJUST_B_START;
  dbug (1, dprintf ("[%06lx] %s,%d: Adjust B1 resource %d %04x...",
    UnMapId (Id), (char   *)(FILE_), __LINE__,
    plci->B1_resource, b1_facilities));
}


static void adjust_b_restore (dword Id, PLCI   *plci, byte Rc)
{
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: adjust_b_restore %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
    if (plci->req_in != 0)
    {
      plci->internal_command = ADJUST_B_RESTORE_1;
      break;
    }
    Rc = OK;
  case ADJUST_B_RESTORE_1:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Adjust B enqueued failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
    }
    plci->adjust_b_parms_msg = NULL;
    plci->adjust_b_facilities = plci->B1_facilities;
    plci->adjust_b_command = ADJUST_B_RESTORE_2;
    plci->adjust_b_ncci = (word)(Id >> 16);
    plci->adjust_b_mode = ADJUST_B_MODE_RESTORE;
    plci->adjust_b_state = ADJUST_B_START;
    dbug (1, dprintf ("[%06lx] %s,%d: Adjust B restore...",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
  case ADJUST_B_RESTORE_2:
    if (adjust_b_process (Id, plci, Rc) != GOOD)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Adjust B restore failed",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
    }
    if (plci->internal_command)
      break;
    break;
  }
}


static void reset_b3_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: reset_b3_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
    plci->adjust_b_parms_msg = NULL;
    plci->adjust_b_facilities = plci->B1_facilities;
    plci->adjust_b_command = RESET_B3_COMMAND_1;
    plci->adjust_b_ncci = (word)(Id >> 16);
    plci->adjust_b_mode = ADJUST_B_MODE_REMOVE_L23 | ADJUST_B_MODE_ASSIGN_L23 | ADJUST_B_MODE_CONNECT;
    plci->adjust_b_state = ADJUST_B_START;
    dbug (1, dprintf ("[%06lx] %s,%d: Reset B3...",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
  case RESET_B3_COMMAND_1:
    Info = adjust_b_process (Id, plci, Rc);
    if (Info != GOOD)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Reset failed",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
      break;
    }
    if (plci->internal_command)
      return;
    break;
  }
/*  sendf (plci->appl, _RESET_B3_R | CONFIRM, Id, plci->number, "w", Info);*/
  sendf(plci->appl,_RESET_B3_I,Id,0,"s","");
}


static void select_b_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;
  byte esc_chi[3];

  dbug (1, dprintf ("[%06lx] %s,%d: select_b_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
    plci->adjust_b_parms_msg = &plci->saved_msg;
    if ((plci->tel == ADV_VOICE) && (plci == plci->adapter->AdvSignalPLCI))
      plci->adjust_b_facilities = plci->B1_facilities | B1_FACILITY_VOICE;
    else
      plci->adjust_b_facilities = plci->B1_facilities & ~B1_FACILITY_VOICE;
    plci->adjust_b_command = SELECT_B_COMMAND_1;
    plci->adjust_b_ncci = (word)(Id >> 16);
    if (plci->saved_msg.parms[0].length == 0)
    {
      plci->adjust_b_mode = ADJUST_B_MODE_SAVE | ADJUST_B_MODE_REMOVE_L23 | ADJUST_B_MODE_SWITCH_L1 |
        ADJUST_B_MODE_NO_RESOURCE;
    }
    else
    {
      plci->adjust_b_mode = ADJUST_B_MODE_SAVE | ADJUST_B_MODE_REMOVE_L23 | ADJUST_B_MODE_SWITCH_L1 |
        ADJUST_B_MODE_ASSIGN_L23 | ADJUST_B_MODE_USER_CONNECT | ADJUST_B_MODE_RESTORE;
    }
    plci->adjust_b_state = ADJUST_B_START;
    dbug (1, dprintf ("[%06lx] %s,%d: Select B protocol...",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
  case SELECT_B_COMMAND_1:
    Info = adjust_b_process (Id, plci, Rc);
    if (Info != GOOD)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: Select B protocol failed",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
      break;
    }
    if (plci->internal_command)
      return;
    if (plci->tel == ADV_VOICE)
    {
      esc_chi[0] = 0x02;
      esc_chi[1] = 0x18;
      esc_chi[2] = plci->b_channel;
      SetVoiceChannel (plci->adapter->AdvCodecPLCI, esc_chi, plci->adapter);
    }
    break;
  }
  sendf (plci->appl, _SELECT_B_REQ | CONFIRM, Id, plci->number, "w", Info);
}


static void fax_connect_ack_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: fax_connect_ack_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
  case FAX_CONNECT_ACK_COMMAND_1:
    if (plci_nl_busy (plci))
    {
      plci->internal_command = FAX_CONNECT_ACK_COMMAND_1;
      return;
    }
    plci->internal_command = FAX_CONNECT_ACK_COMMAND_2;
    plci->NData[0].P = plci->fax_connect_info_buffer;
    plci->NData[0].PLength = plci->fax_connect_info_length;
    plci->NL.X = plci->NData;
    plci->NL.ReqCh = 0;
    plci->NL.Req = plci->nl_req = (byte) N_CONNECT_ACK;
    plci->adapter->request (&plci->NL);
    return;
  case FAX_CONNECT_ACK_COMMAND_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: FAX issue CONNECT ACK failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      break;
    }
  }
  if ((plci->ncpi_state & NCPI_VALID_CONNECT_B3_ACT)
   && !(plci->ncpi_state & NCPI_CONNECT_B3_ACT_SENT))
  {
    if (plci->B3_prot == 4)
      sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"s","");
    else
      sendf(plci->appl,_CONNECT_B3_ACTIVE_I,Id,0,"S",plci->ncpi_buffer);
    plci->ncpi_state |= NCPI_CONNECT_B3_ACT_SENT;
  }
}


static void fax_edata_ack_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: fax_edata_ack_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
  case FAX_EDATA_ACK_COMMAND_1:
    if (plci_nl_busy (plci))
    {
      plci->internal_command = FAX_EDATA_ACK_COMMAND_1;
      return;
    }
    plci->internal_command = FAX_EDATA_ACK_COMMAND_2;
    plci->NData[0].P = plci->fax_connect_info_buffer;
    plci->NData[0].PLength = plci->fax_edata_ack_length;
    plci->NL.X = plci->NData;
    plci->NL.ReqCh = 0;
    plci->NL.Req = plci->nl_req = (byte) N_EDATA;
    plci->adapter->request (&plci->NL);
    return;
  case FAX_EDATA_ACK_COMMAND_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: FAX issue EDATA ACK failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      break;
    }
  }
}


static void fax_connect_info_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: fax_connect_info_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
  case FAX_CONNECT_INFO_COMMAND_1:
    if (plci_nl_busy (plci))
    {
      plci->internal_command = FAX_CONNECT_INFO_COMMAND_1;
      return;
    }
    plci->internal_command = FAX_CONNECT_INFO_COMMAND_2;
    plci->NData[0].P = plci->fax_connect_info_buffer;
    plci->NData[0].PLength = plci->fax_connect_info_length;
    plci->NL.X = plci->NData;
    plci->NL.ReqCh = 0;
    plci->NL.Req = plci->nl_req = (byte) N_EDATA;
    plci->adapter->request (&plci->NL);
    return;
  case FAX_CONNECT_INFO_COMMAND_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: FAX setting connect info failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      Info = _WRONG_STATE;
      break;
    }
    if (plci_nl_busy (plci))
    {
      plci->internal_command = FAX_CONNECT_INFO_COMMAND_2;
      return;
    }
    plci->command = _CONNECT_B3_R;
    nl_req_ncci (plci, N_CONNECT, 0);
    send_req (plci);
    return;
  }
  sendf (plci->appl, _CONNECT_B3_R | CONFIRM, Id, plci->number, "w", Info);
}


static void fax_adjust_b23_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: fax_adjust_b23_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
    plci->adjust_b_parms_msg = NULL;
    plci->adjust_b_facilities = plci->B1_facilities;
    plci->adjust_b_command = FAX_ADJUST_B23_COMMAND_1;
    plci->adjust_b_ncci = (word)(Id >> 16);
    plci->adjust_b_mode = ADJUST_B_MODE_REMOVE_L23 | ADJUST_B_MODE_ASSIGN_L23;
    plci->adjust_b_state = ADJUST_B_START;
    dbug (1, dprintf ("[%06lx] %s,%d: FAX adjust B23...",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
  case FAX_ADJUST_B23_COMMAND_1:
    Info = adjust_b_process (Id, plci, Rc);
    if (Info != GOOD)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: FAX adjust failed",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
      break;
    }
    if (plci->internal_command)
      return;
  case FAX_ADJUST_B23_COMMAND_2:
    if (plci_nl_busy (plci))
    {
      plci->internal_command = FAX_ADJUST_B23_COMMAND_2;
      return;
    }
    plci->command = _CONNECT_B3_R;
    nl_req_ncci (plci, N_CONNECT, 0);
    send_req (plci);
    return;
  }
  sendf (plci->appl, _CONNECT_B3_R | CONFIRM, Id, plci->number, "w", Info);
}


static void fax_disconnect_command (dword Id, PLCI   *plci, byte Rc)
{
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: fax_disconnect_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
    plci->internal_command = FAX_DISCONNECT_COMMAND_1;
    return;
  case FAX_DISCONNECT_COMMAND_1:
  case FAX_DISCONNECT_COMMAND_2:
  case FAX_DISCONNECT_COMMAND_3:
    if ((Rc != OK) && (Rc != OK_FC) && (Rc != 0))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: FAX disconnect EDATA failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      break;
    }
    if (Rc == OK)
    {
      if ((internal_command == FAX_DISCONNECT_COMMAND_1)
       || (internal_command == FAX_DISCONNECT_COMMAND_2))
      {
        plci->internal_command = FAX_DISCONNECT_COMMAND_2;
      }
    }
    else if (Rc == 0)
    {
      if (internal_command == FAX_DISCONNECT_COMMAND_1)
        plci->internal_command = FAX_DISCONNECT_COMMAND_3;
    }
    return;
  }
}



static void rtp_connect_b3_req_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: rtp_connect_b3_req_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
  case RTP_CONNECT_B3_REQ_COMMAND_1:
    if (plci_nl_busy (plci))
    {
      plci->internal_command = RTP_CONNECT_B3_REQ_COMMAND_1;
      return;
    }
    plci->internal_command = RTP_CONNECT_B3_REQ_COMMAND_2;
    nl_req_ncci (plci, N_CONNECT, 0);
    send_req (plci);
    return;
  case RTP_CONNECT_B3_REQ_COMMAND_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: RTP setting connect info failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      Info = _WRONG_STATE;
      break;
    }
    if (plci_nl_busy (plci))
    {
      plci->internal_command = RTP_CONNECT_B3_REQ_COMMAND_2;
      return;
    }
    plci->internal_command = RTP_CONNECT_B3_REQ_COMMAND_3;
    plci->NData[0].PLength = plci->internal_req_buffer[0];
    plci->NData[0].P = plci->internal_req_buffer + 1;
    plci->NL.X = plci->NData;
    plci->NL.ReqCh = 0;
    plci->NL.Req = plci->nl_req = (byte) N_UDATA;
    plci->adapter->request (&plci->NL);
    break;
  case RTP_CONNECT_B3_REQ_COMMAND_3:
    return;
  }
  sendf (plci->appl, _CONNECT_B3_R | CONFIRM, Id, plci->number, "w", Info);
}


static void rtp_connect_b3_res_command (dword Id, PLCI   *plci, byte Rc)
{
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: rtp_connect_b3_res_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
  case RTP_CONNECT_B3_RES_COMMAND_1:
    if (plci_nl_busy (plci))
    {
      plci->internal_command = RTP_CONNECT_B3_RES_COMMAND_1;
      return;
    }
    plci->internal_command = RTP_CONNECT_B3_RES_COMMAND_2;
    nl_req_ncci (plci, N_CONNECT_ACK, (byte)(Id >> 16));
    send_req (plci);
    return;
  case RTP_CONNECT_B3_RES_COMMAND_2:
    if ((Rc != OK) && (Rc != OK_FC))
    {
      dbug (1, dprintf ("[%06lx] %s,%d: RTP setting connect resp info failed %02x",
        UnMapId (Id), (char   *)(FILE_), __LINE__, Rc));
      Info = _WRONG_STATE;
      break;
    }
    if (plci_nl_busy (plci))
    {
      plci->internal_command = RTP_CONNECT_B3_RES_COMMAND_2;
      return;
    }
    sendf (plci->appl, _CONNECT_B3_ACTIVE_I, Id, 0, "s", "");
    plci->internal_command = RTP_CONNECT_B3_RES_COMMAND_3;
    plci->NData[0].PLength = plci->internal_req_buffer[0];
    plci->NData[0].P = plci->internal_req_buffer + 1;
    plci->NL.X = plci->NData;
    plci->NL.ReqCh = 0;
    plci->NL.Req = plci->nl_req = (byte) N_UDATA;
    plci->adapter->request (&plci->NL);
    return;
  case RTP_CONNECT_B3_RES_COMMAND_3:
    return;
  }
}



static void hold_save_command (dword Id, PLCI   *plci, byte Rc)
{
    byte SS_Ind[] = "\x05\x02\x00\x02\x00\x00"; /* Hold_Ind struct*/
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: hold_save_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    if (!plci->NL.Id)
      break;
    plci->command = 0;
    plci->adjust_b_parms_msg = NULL;
    plci->adjust_b_facilities = plci->B1_facilities;
    plci->adjust_b_command = HOLD_SAVE_COMMAND_1;
    plci->adjust_b_ncci = (word)(Id >> 16);
    plci->adjust_b_mode = ADJUST_B_MODE_SAVE | ADJUST_B_MODE_REMOVE_L23;
    plci->adjust_b_state = ADJUST_B_START;
    dbug (1, dprintf ("[%06lx] %s,%d: HOLD save...",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
  case HOLD_SAVE_COMMAND_1:
    Info = adjust_b_process (Id, plci, Rc);
    if (Info != GOOD)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: HOLD save failed",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
      break;
    }
    if (plci->internal_command)
      return;
  }
  sendf (plci->appl, _FACILITY_I, Id & 0xffffL, 0, "ws", 3, SS_Ind);
}


static void retrieve_restore_command (dword Id, PLCI   *plci, byte Rc)
{
    byte SS_Ind[] = "\x05\x03\x00\x02\x00\x00"; /* Retrieve_Ind struct*/
  word Info;
  word internal_command;

  dbug (1, dprintf ("[%06lx] %s,%d: retrieve_restore_command %02x %04x",
    UnMapId (Id), (char   *)(FILE_), __LINE__, Rc, plci->internal_command));

  Info = GOOD;
  internal_command = plci->internal_command;
  plci->internal_command = 0;
  switch (internal_command)
  {
  default:
    plci->command = 0;
    plci->adjust_b_parms_msg = NULL;
    plci->adjust_b_facilities = plci->B1_facilities;
    plci->adjust_b_command = RETRIEVE_RESTORE_COMMAND_1;
    plci->adjust_b_ncci = (word)(Id >> 16);
    plci->adjust_b_mode = ADJUST_B_MODE_ASSIGN_L23 | ADJUST_B_MODE_USER_CONNECT | ADJUST_B_MODE_RESTORE;
    plci->adjust_b_state = ADJUST_B_START;
    dbug (1, dprintf ("[%06lx] %s,%d: RETRIEVE restore...",
      UnMapId (Id), (char   *)(FILE_), __LINE__));
  case RETRIEVE_RESTORE_COMMAND_1:
    Info = adjust_b_process (Id, plci, Rc);
    if (Info != GOOD)
    {
      dbug (1, dprintf ("[%06lx] %s,%d: RETRIEVE restore failed",
        UnMapId (Id), (char   *)(FILE_), __LINE__));
      break;
    }
    if (plci->internal_command)
      return;
  }
  sendf (plci->appl, _FACILITY_I, Id & 0xffffL, 0, "ws", 3, SS_Ind);
}


static void init_b1_config (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: init_b1_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  plci->B1_resource = 0;
  plci->B1_facilities = 0;

  plci->li_bchannel_id = 0;
  mixer_clear_config (plci);


  ec_clear_config (plci);


  dtmf_rec_clear_config (plci);
  dtmf_send_clear_config (plci);
  dtmf_parameter_clear_config (plci);

  adv_voice_clear_config (plci);
  adjust_b_clear (plci);
}


static void clear_b1_config (PLCI   *plci)
{

  dbug (1, dprintf ("[%06lx] %s,%d: clear_b1_config",
    (dword)((plci->Id << 8) | UnMapController (plci->adapter->Id)),
    (char   *)(FILE_), __LINE__));

  adv_voice_clear_config (plci);
  adjust_b_clear (plci);

  ec_clear_config (plci);


  dtmf_rec_clear_config (plci);
  dtmf_send_clear_config (plci);
  dtmf_parameter_clear_config (plci);


  if ((plci->li_bchannel_id != 0)
   && (li_config_table[plci->adapter->li_base + (plci->li_bchannel_id - 1)].plci == plci))
  {
    mixer_clear_config (plci);
    li_config_table[plci->adapter->li_base + (plci->li_bchannel_id - 1)].plci = NULL;
    plci->li_bchannel_id = 0;
  }

  plci->B1_resource = 0;
  plci->B1_facilities = 0;
}


/* -----------------------------------------------------------------
                XON protocol local helpers
   ----------------------------------------------------------------- */
static void channel_flow_control_remove (PLCI   * plci) {
  DIVA_CAPI_ADAPTER   * a = plci->adapter;
  word i;
  for(i=1;i<MAX_NL_CHANNEL+1;i++) {
    if (a->ch_flow_plci[i] == plci->Id) {
      a->ch_flow_plci[i] = 0;
      a->ch_flow_control[i] = 0;
    }
  }
}

static void channel_x_on (PLCI   * plci, byte ch) {
  DIVA_CAPI_ADAPTER   * a = plci->adapter;
  if (a->ch_flow_control[ch] & N_XON_SENT) {
    a->ch_flow_control[ch] &= ~N_XON_SENT;
  }
}

static void channel_x_off (PLCI   * plci, byte ch, byte flag) {
  DIVA_CAPI_ADAPTER   * a = plci->adapter;
  if ((a->ch_flow_control[ch] & N_RX_FLOW_CONTROL_MASK) == 0) {
    a->ch_flow_control[ch] |= (N_CH_XOFF | flag);
    a->ch_flow_plci[ch] = plci->Id;
    a->ch_flow_control_pending++;
  }
}

static void channel_request_xon (PLCI   * plci, byte ch) {
  DIVA_CAPI_ADAPTER   * a = plci->adapter;

  if (a->ch_flow_control[ch] & N_CH_XOFF) {
    a->ch_flow_control[ch] |= N_XON_REQ;
    a->ch_flow_control[ch] &= ~N_CH_XOFF;
    a->ch_flow_control[ch] &= ~N_XON_CONNECT_IND;
  }
}

static void channel_xmit_extended_xon (PLCI   * plci) {
  DIVA_CAPI_ADAPTER   * a;
  int max_ch = ARRAY_SIZE(a->ch_flow_control);
  int i, one_requested = 0;

  if ((!plci) || (!plci->Id) || ((a = plci->adapter) == NULL)) {
    return;
  }

  for (i = 0; i < max_ch; i++) {
    if ((a->ch_flow_control[i] & N_CH_XOFF) &&
        (a->ch_flow_control[i] & N_XON_CONNECT_IND) &&
        (plci->Id == a->ch_flow_plci[i])) {
      channel_request_xon (plci, (byte)i);
      one_requested = 1;
    }
  }

  if (one_requested) {
    channel_xmit_xon (plci);
  }
}

/*
  Try to xmit next X_ON
  */
static int find_channel_with_pending_x_on (DIVA_CAPI_ADAPTER   * a, PLCI   * plci) {
  int max_ch = ARRAY_SIZE(a->ch_flow_control);
  int i;

  if (!(plci->adapter->manufacturer_features & MANUFACTURER_FEATURE_XONOFF_FLOW_CONTROL)) {
    return (0);
  }

  if (a->last_flow_control_ch >= max_ch) {
    a->last_flow_control_ch = 1;
  }
  for (i=a->last_flow_control_ch; i < max_ch; i++) {
    if ((a->ch_flow_control[i] & N_XON_REQ) &&
        (plci->Id == a->ch_flow_plci[i])) {
      a->last_flow_control_ch = i+1;
      return (i);
    }
  }

  for (i = 1; i < a->last_flow_control_ch; i++) {
    if ((a->ch_flow_control[i] & N_XON_REQ) &&
        (plci->Id == a->ch_flow_plci[i])) {
      a->last_flow_control_ch = i+1;
      return (i);
    }
  }

  return (0);
}

static void channel_xmit_xon (PLCI   * plci) {
  DIVA_CAPI_ADAPTER   * a = plci->adapter;
  byte ch;

  if (plci->nl_req || !plci->NL.Id || plci->nl_remove_id) {
    return;
  }
  if ((ch = (byte)find_channel_with_pending_x_on (a, plci)) == 0) {
    return;
  }
  a->ch_flow_control[ch] &= ~N_XON_REQ;
  a->ch_flow_control[ch] |= N_XON_SENT;

  plci->NL.Req = plci->nl_req = (byte)N_XON;
  plci->NL.ReqCh         = ch;
  plci->NL.X             = plci->NData;
  plci->NL.XNum          = 1;
  plci->NData[0].P       = &plci->RBuffer[0];
  plci->NData[0].PLength = 0;

  plci->adapter->request(&plci->NL);
}

static int channel_can_xon (PLCI   * plci, byte ch) {
  APPL   * APPLptr;
  DIVA_CAPI_ADAPTER   * a;
  word NCCIcode;
  dword count;
  word Num;
  word i;

  APPLptr = plci->appl;
  a = plci->adapter;

  if (!APPLptr)
    return (0);

  NCCIcode = a->ch_ncci[ch] | (((word) a->Id) << 8);

                /* count all buffers within the Application pool    */
                /* belonging to the same NCCI. XON if a first is    */
                /* used.                                            */
  count = 0;
  Num = 0xffff;
  for(i=0; i<APPLptr->MaxBuffer; i++) {
    if(NCCIcode==APPLptr->DataNCCI[i]) count++;
    if(!APPLptr->DataNCCI[i] && Num==0xffff) Num = i;
  }
  if ((count > 2) || (Num == 0xffff)) {
    return (0);
  }
  return (1);
}


/*------------------------------------------------------------------*/

static word CPN_filter_ok(byte   *cpn,DIVA_CAPI_ADAPTER   * a,word offset)
{
  return 1;
}



/**********************************************************************************/
/* function groups the listening applications according to the CIP mask and the   */
/* Info_Mask. Each group gets just one Connect_Ind. Some application manufacturer */
/* are not multi-instance capable, so they start e.g. 30 applications what causes */
/* big problems on application level (one call, 30 Connect_Ind, ect). The         */
/* function must be enabled by setting "a->group_optimization_enabled" from the   */
/* OS specific part (per adapter).                                                */
/**********************************************************************************/
static void group_optimization(DIVA_CAPI_ADAPTER   * a, PLCI   * plci)
{
  word i,j,k,busy,group_found;
  dword info_mask_group[MAX_CIP_TYPES];
  dword cip_mask_group[MAX_CIP_TYPES];
  word appl_number_group_type[MAX_APPL];
  PLCI   *auxplci;

  set_group_ind_mask (plci); /* all APPLs within this inc. call are allowed to dial in */

  if(!a->group_optimization_enabled)
  {
    dbug(1,dprintf("No group optimization"));
    return;
  }

  dbug(1,dprintf("Group optimization = 0x%x...", a->group_optimization_enabled));

  for(i=0;i<MAX_CIP_TYPES;i++)
  {
    info_mask_group[i] = 0;
    cip_mask_group [i] = 0;
  }
  for(i=0;i<MAX_APPL;i++)
  {
    appl_number_group_type[i] = 0;
  }
  for(i=0; i<max_appl; i++) /* check if any multi instance capable application is present */
  {  /* group_optimization set to 1 means not to optimize multi-instance capable applications (default) */
    if(application[i].Id && (application[i].MaxNCCI) > 1 && (a->CIP_Mask[i])  && (a->group_optimization_enabled ==1) )
    {
      dbug(1,dprintf("Multi-Instance capable, no optimization required"));
      return; /* allow good application unfiltered access */
    }
  }
  for(i=0; i<max_appl; i++) /* Build CIP Groups */
  {
    if(application[i].Id && a->CIP_Mask[i] )
    {
      for(k=0,busy=false; k<a->max_plci; k++)
      {
        if(a->plci[k].Id) 
        {
          auxplci = &a->plci[k];
          if(auxplci->appl == &application[i]) /* application has a busy PLCI */
          {
            busy = true;
            dbug(1,dprintf("Appl 0x%x is busy",i+1));
          }
          else if(test_c_ind_mask_bit (auxplci, i)) /* application has an incoming call pending */
          {
            busy = true;
            dbug(1,dprintf("Appl 0x%x has inc. call pending",i+1));
          }
        }
      }

      for(j=0,group_found=0; j<=(MAX_CIP_TYPES) && !busy &&!group_found; j++)     /* build groups with free applications only */
      {
        if(j==MAX_CIP_TYPES)       /* all groups are in use but group still not found */
        {                           /* the MAX_CIP_TYPES group enables all calls because of field overflow */
          appl_number_group_type[i] = MAX_CIP_TYPES;
          group_found=true;
          dbug(1,dprintf("Field overflow appl 0x%x",i+1));
        }
        else if( (info_mask_group[j]==a->CIP_Mask[i]) && (cip_mask_group[j]==a->Info_Mask[i]) )  
        {                                      /* is group already present ?                  */
          appl_number_group_type[i] = j|0x80;  /* store the group number for each application */
          group_found=true;
          dbug(1,dprintf("Group 0x%x found with appl 0x%x, CIP=0x%lx",appl_number_group_type[i],i+1,info_mask_group[j]));
        }
        else if(!info_mask_group[j])
        {                                      /* establish a new group                       */
          appl_number_group_type[i] = j|0x80;  /* store the group number for each application */
          info_mask_group[j] = a->CIP_Mask[i]; /* store the new CIP mask for the new group    */
          cip_mask_group[j] = a->Info_Mask[i]; /* store the new Info_Mask for this new group  */
          group_found=true;
          dbug(1,dprintf("New Group 0x%x established with appl 0x%x, CIP=0x%lx",appl_number_group_type[i],i+1,info_mask_group[j]));
        }
      }
    }
  }
        
  for(i=0; i<max_appl; i++) /* Build group_optimization_mask_table */
  {
    if(appl_number_group_type[i]) /* application is free, has listens and is member of a group */
    {
      if(appl_number_group_type[i] == MAX_CIP_TYPES)
      {
        dbug(1,dprintf("OverflowGroup 0x%x, valid appl = 0x%x, call enabled",appl_number_group_type[i],i+1));
      }
      else
      {
        dbug(1,dprintf("Group 0x%x, valid appl = 0x%x",appl_number_group_type[i],i+1));
        for(j=i+1; j<max_appl; j++)   /* search other group members and mark them as busy        */
        {
          if(appl_number_group_type[i] == appl_number_group_type[j]) 
          {
            dbug(1,dprintf("Appl 0x%x is member of group 0x%x, no call",j+1,appl_number_group_type[j]));
            clear_group_ind_mask_bit (plci, j);           /* disable call on other group members */
            appl_number_group_type[j] = 0;       /* remove disabled group member from group list */
          }
        }
      }
    }
    else                                                 /* application should not get a call */
    {
      clear_group_ind_mask_bit (plci, i);
    }
  }

}



/* OS notifies the driver about a application Capi_Register */
word CapiRegister(word id)
{
  word i,j,appls_found;

  PLCI   *plci;
  DIVA_CAPI_ADAPTER   *a;

  for(i=0,appls_found=0; i<max_appl; i++)
  {
    if( application[i].Id && (application[i].Id!=id) )
    {
      appls_found++;                       /* an application has been found */
    }
  }

  if(appls_found) return true;
  for(i=0; i<max_adapter; i++)                   /* scan all adapters...    */
  {
    a = &adapter[i];
    if(a->request)
    {
      if(a->flag_dynamic_l1_down)  /* remove adapter from L1 tristate (Huntgroup) */
      {
        if(!appls_found)           /* first application does a capi register   */
        {
          if((j=get_plci(a)))                    /* activate L1 of all adapters */
          {
            plci = &a->plci[j-1];
            plci->command = 0;
            add_p(plci,OAD,"\x01\xfd");
            add_p(plci,CAI,"\x01\x80");
            add_p(plci,UID,"\x06\x43\x61\x70\x69\x32\x30");
            add_p(plci,SHIFT|6,NULL);
            add_p(plci,SIN,"\x02\x00\x00");
            plci->internal_command = START_L1_SIG_ASSIGN_PEND;
            sig_req(plci,ASSIGN,DSIG_ID);
            add_p(plci,FTY,"\x02\xff\x07"); /* l1 start */
            sig_req(plci,SIG_CTRL,0);
            send_req(plci);
          }
        }
      }
    }
  }
  return false;
}

/*------------------------------------------------------------------*/

/* Functions for virtual Switching e.g. Transfer by join, Conference */

static void VSwitchReqInd(PLCI   *plci, dword Id, byte   **parms)
{
 word i;
 /* Format of vswitch_t:
 0 byte length
 1 byte VSWITCHIE
 2 byte VSWITCH_REQ/VSWITCH_IND
 3 byte reserved
 4 word VSwitchcommand
 6 word returnerror
 8... Params
 */
 if(!plci ||
  !plci->appl ||
  !plci->State ||
  plci->Sig.Ind==NCR_FACILITY
  )
  return;
 
 for(i=0;i<MAX_MULTI_IE;i++)
 {
        if(!parms[i][0]) continue;
  if(parms[i][0]<7)
  {
   parms[i][0]=0; /* kill it */
   continue;
  }
  dbug(1,dprintf("VSwitchReqInd(%d)",parms[i][4]));
  switch(parms[i][4])
  {
  case VSJOIN:
   if(!plci->relatedPTYPLCI ||
    (plci->ptyState!=S_ECT && plci->relatedPTYPLCI->ptyState!=S_ECT))
   { /* Error */
    break;
   }
   /* remember all necessary informations */
   if(parms[i][0]!=11 || parms[i][8]!=3) /* Length Test */
   {
    break;
   }
   if(parms[i][2]==VSWITCH_IND && parms[i][9]==1)
   {   /* first indication after ECT-Request on Consultation Call */
    plci->vswitchstate=parms[i][9];
    parms[i][9]=2; /* State */
    /* now ask first Call to join */
   }
   else if(parms[i][2]==VSWITCH_REQ && parms[i][9]==3)
   { /* Answer of VSWITCH_REQ from first Call */
    plci->vswitchstate=parms[i][9];
    /* tell consultation call to join
    and the protocol capabilities of the first call */
   }
   else
   { /* Error */
    break;
   }    
   plci->vsprot=parms[i][10]; /* protocol */
   plci->vsprotdialect=parms[i][11]; /* protocoldialect */
   /* send join request to related PLCI */
   parms[i][1]=VSWITCHIE;
   parms[i][2]=VSWITCH_REQ;
   
   plci->relatedPTYPLCI->command = 0;
   plci->relatedPTYPLCI->internal_command = VSWITCH_REQ_PEND;
   add_p(plci->relatedPTYPLCI,ESC,&parms[i][0]);
   sig_req(plci->relatedPTYPLCI,VSWITCH_REQ,0);
   send_req(plci->relatedPTYPLCI);
   break;
  case VSTRANSPORT:
  default:
   if(plci->relatedPTYPLCI &&
    plci->vswitchstate==3 &&
    plci->relatedPTYPLCI->vswitchstate==3)
   {
    add_p(plci->relatedPTYPLCI,ESC,&parms[i][0]);
    sig_req(plci->relatedPTYPLCI,VSWITCH_REQ,0);
    send_req(plci->relatedPTYPLCI);
   }
   break;
  }  
  parms[i][0]=0; /* kill it */
 }
}


/*------------------------------------------------------------------*/

static int diva_get_dma_descriptor (PLCI   *plci, dword   *dma_magic) {
  ENTITY e;
  IDI_SYNC_REQ* pReq = (IDI_SYNC_REQ*)&e;

  if (!(diva_xdi_extended_features & DIVA_CAPI_XDI_PROVIDES_RX_DMA)) {
    return (-1);
  }

  pReq->xdi_dma_descriptor_operation.Req = 0;
  pReq->xdi_dma_descriptor_operation.Rc = IDI_SYNC_REQ_DMA_DESCRIPTOR_OPERATION;

  pReq->xdi_dma_descriptor_operation.info.operation =     IDI_SYNC_REQ_DMA_DESCRIPTOR_ALLOC;
  pReq->xdi_dma_descriptor_operation.info.descriptor_number  = -1;
  pReq->xdi_dma_descriptor_operation.info.descriptor_address = NULL;
  pReq->xdi_dma_descriptor_operation.info.descriptor_magic   = 0;

  e.user[0] = plci->adapter->Id - 1;
  plci->adapter->request((ENTITY*)pReq);

  if (!pReq->xdi_dma_descriptor_operation.info.operation &&
      (pReq->xdi_dma_descriptor_operation.info.descriptor_number >= 0) &&
      pReq->xdi_dma_descriptor_operation.info.descriptor_magic) {
    *dma_magic = pReq->xdi_dma_descriptor_operation.info.descriptor_magic;
    dbug(3,dprintf("dma_alloc, a:%d (%d-%08x)",
         plci->adapter->Id,
         pReq->xdi_dma_descriptor_operation.info.descriptor_number,
         *dma_magic));
    return (pReq->xdi_dma_descriptor_operation.info.descriptor_number);
  } else {
    dbug(1,dprintf("dma_alloc failed"));
    return (-1);
  }
}

static void diva_free_dma_descriptor (PLCI   *plci, int nr) {
  ENTITY e;
  IDI_SYNC_REQ* pReq = (IDI_SYNC_REQ*)&e;

  if (nr < 0) {
    return;
  }

  pReq->xdi_dma_descriptor_operation.Req = 0;
  pReq->xdi_dma_descriptor_operation.Rc = IDI_SYNC_REQ_DMA_DESCRIPTOR_OPERATION;

  pReq->xdi_dma_descriptor_operation.info.operation =                                                IDI_SYNC_REQ_DMA_DESCRIPTOR_FREE;
  pReq->xdi_dma_descriptor_operation.info.descriptor_number  = nr;
  pReq->xdi_dma_descriptor_operation.info.descriptor_address = NULL;
  pReq->xdi_dma_descriptor_operation.info.descriptor_magic   = 0;

  e.user[0] = plci->adapter->Id - 1;
  plci->adapter->request((ENTITY*)pReq);

  if (!pReq->xdi_dma_descriptor_operation.info.operation) {
    dbug(1,dprintf("dma_free(%d)", nr));
  } else {
    dbug(1,dprintf("dma_free failed (%d)", nr));
  }
}

/*------------------------------------------------------------------*/
