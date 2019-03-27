/*
 * Automatically generated from ../../../sntp/libevent/test/regress.rpc
 */

#ifndef EVENT_RPCOUT__________SNTP_LIBEVENT_TEST_REGRESS_RPC_
#define EVENT_RPCOUT__________SNTP_LIBEVENT_TEST_REGRESS_RPC_

#include <event2/util.h> /* for ev_uint*_t */
#include <event2/rpc.h>

struct msg;
struct kill;
struct run;

/* Tag definition for msg */
enum msg_ {
  MSG_FROM_NAME=1,
  MSG_TO_NAME=2,
  MSG_ATTACK=3,
  MSG_RUN=4,
  MSG_MAX_TAGS
};

/* Structure declaration for msg */
struct msg_access_ {
  int (*from_name_assign)(struct msg *, const char *);
  int (*from_name_get)(struct msg *, char * *);
  int (*to_name_assign)(struct msg *, const char *);
  int (*to_name_get)(struct msg *, char * *);
  int (*attack_assign)(struct msg *, const struct kill*);
  int (*attack_get)(struct msg *, struct kill* *);
  int (*run_assign)(struct msg *, int, const struct run*);
  int (*run_get)(struct msg *, int, struct run* *);
  struct run*  (*run_add)(struct msg *msg);
};

struct msg {
  struct msg_access_ *base;

  char *from_name_data;
  char *to_name_data;
  struct kill* attack_data;
  struct run* *run_data;
  int run_length;
  int run_num_allocated;

  ev_uint8_t from_name_set;
  ev_uint8_t to_name_set;
  ev_uint8_t attack_set;
  ev_uint8_t run_set;
};

struct msg *msg_new(void);
struct msg *msg_new_with_arg(void *);
void msg_free(struct msg *);
void msg_clear(struct msg *);
void msg_marshal(struct evbuffer *, const struct msg *);
int msg_unmarshal(struct msg *, struct evbuffer *);
int msg_complete(struct msg *);
void evtag_marshal_msg(struct evbuffer *, ev_uint32_t,
    const struct msg *);
int evtag_unmarshal_msg(struct evbuffer *, ev_uint32_t,
    struct msg *);
int msg_from_name_assign(struct msg *, const char *);
int msg_from_name_get(struct msg *, char * *);
int msg_to_name_assign(struct msg *, const char *);
int msg_to_name_get(struct msg *, char * *);
int msg_attack_assign(struct msg *, const struct kill*);
int msg_attack_get(struct msg *, struct kill* *);
int msg_run_assign(struct msg *, int, const struct run*);
int msg_run_get(struct msg *, int, struct run* *);
struct run*  msg_run_add(struct msg *msg);
/* --- msg done --- */

/* Tag definition for kill */
enum kill_ {
  KILL_WEAPON=65825,
  KILL_ACTION=2,
  KILL_HOW_OFTEN=3,
  KILL_MAX_TAGS
};

/* Structure declaration for kill */
struct kill_access_ {
  int (*weapon_assign)(struct kill *, const char *);
  int (*weapon_get)(struct kill *, char * *);
  int (*action_assign)(struct kill *, const char *);
  int (*action_get)(struct kill *, char * *);
  int (*how_often_assign)(struct kill *, int, const ev_uint32_t);
  int (*how_often_get)(struct kill *, int, ev_uint32_t *);
  ev_uint32_t * (*how_often_add)(struct kill *msg, const ev_uint32_t value);
};

struct kill {
  struct kill_access_ *base;

  char *weapon_data;
  char *action_data;
  ev_uint32_t *how_often_data;
  int how_often_length;
  int how_often_num_allocated;

  ev_uint8_t weapon_set;
  ev_uint8_t action_set;
  ev_uint8_t how_often_set;
};

struct kill *kill_new(void);
struct kill *kill_new_with_arg(void *);
void kill_free(struct kill *);
void kill_clear(struct kill *);
void kill_marshal(struct evbuffer *, const struct kill *);
int kill_unmarshal(struct kill *, struct evbuffer *);
int kill_complete(struct kill *);
void evtag_marshal_kill(struct evbuffer *, ev_uint32_t,
    const struct kill *);
int evtag_unmarshal_kill(struct evbuffer *, ev_uint32_t,
    struct kill *);
int kill_weapon_assign(struct kill *, const char *);
int kill_weapon_get(struct kill *, char * *);
int kill_action_assign(struct kill *, const char *);
int kill_action_get(struct kill *, char * *);
int kill_how_often_assign(struct kill *, int, const ev_uint32_t);
int kill_how_often_get(struct kill *, int, ev_uint32_t *);
ev_uint32_t * kill_how_often_add(struct kill *msg, const ev_uint32_t value);
/* --- kill done --- */

/* Tag definition for run */
enum run_ {
  RUN_HOW=1,
  RUN_SOME_BYTES=2,
  RUN_FIXED_BYTES=3,
  RUN_NOTES=4,
  RUN_LARGE_NUMBER=5,
  RUN_OTHER_NUMBERS=6,
  RUN_MAX_TAGS
};

/* Structure declaration for run */
struct run_access_ {
  int (*how_assign)(struct run *, const char *);
  int (*how_get)(struct run *, char * *);
  int (*some_bytes_assign)(struct run *, const ev_uint8_t *, ev_uint32_t);
  int (*some_bytes_get)(struct run *, ev_uint8_t * *, ev_uint32_t *);
  int (*fixed_bytes_assign)(struct run *, const ev_uint8_t *);
  int (*fixed_bytes_get)(struct run *, ev_uint8_t **);
  int (*notes_assign)(struct run *, int, const char *);
  int (*notes_get)(struct run *, int, char * *);
  char * * (*notes_add)(struct run *msg, const char * value);
  int (*large_number_assign)(struct run *, const ev_uint64_t);
  int (*large_number_get)(struct run *, ev_uint64_t *);
  int (*other_numbers_assign)(struct run *, int, const ev_uint32_t);
  int (*other_numbers_get)(struct run *, int, ev_uint32_t *);
  ev_uint32_t * (*other_numbers_add)(struct run *msg, const ev_uint32_t value);
};

struct run {
  struct run_access_ *base;

  char *how_data;
  ev_uint8_t *some_bytes_data;
  ev_uint32_t some_bytes_length;
  ev_uint8_t fixed_bytes_data[24];
  char * *notes_data;
  int notes_length;
  int notes_num_allocated;
  ev_uint64_t large_number_data;
  ev_uint32_t *other_numbers_data;
  int other_numbers_length;
  int other_numbers_num_allocated;

  ev_uint8_t how_set;
  ev_uint8_t some_bytes_set;
  ev_uint8_t fixed_bytes_set;
  ev_uint8_t notes_set;
  ev_uint8_t large_number_set;
  ev_uint8_t other_numbers_set;
};

struct run *run_new(void);
struct run *run_new_with_arg(void *);
void run_free(struct run *);
void run_clear(struct run *);
void run_marshal(struct evbuffer *, const struct run *);
int run_unmarshal(struct run *, struct evbuffer *);
int run_complete(struct run *);
void evtag_marshal_run(struct evbuffer *, ev_uint32_t,
    const struct run *);
int evtag_unmarshal_run(struct evbuffer *, ev_uint32_t,
    struct run *);
int run_how_assign(struct run *, const char *);
int run_how_get(struct run *, char * *);
int run_some_bytes_assign(struct run *, const ev_uint8_t *, ev_uint32_t);
int run_some_bytes_get(struct run *, ev_uint8_t * *, ev_uint32_t *);
int run_fixed_bytes_assign(struct run *, const ev_uint8_t *);
int run_fixed_bytes_get(struct run *, ev_uint8_t **);
int run_notes_assign(struct run *, int, const char *);
int run_notes_get(struct run *, int, char * *);
char * * run_notes_add(struct run *msg, const char * value);
int run_large_number_assign(struct run *, const ev_uint64_t);
int run_large_number_get(struct run *, ev_uint64_t *);
int run_other_numbers_assign(struct run *, int, const ev_uint32_t);
int run_other_numbers_get(struct run *, int, ev_uint32_t *);
ev_uint32_t * run_other_numbers_add(struct run *msg, const ev_uint32_t value);
/* --- run done --- */

#endif  /* EVENT_RPCOUT__________SNTP_LIBEVENT_TEST_REGRESS_RPC_ */
