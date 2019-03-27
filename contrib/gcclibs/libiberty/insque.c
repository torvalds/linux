/* insque(3C) routines
   This file is in the public domain.  */

/*

@deftypefn Supplemental void insque (struct qelem *@var{elem}, struct qelem *@var{pred})
@deftypefnx Supplemental void remque (struct qelem *@var{elem})

Routines to manipulate queues built from doubly linked lists.  The
@code{insque} routine inserts @var{elem} in the queue immediately
after @var{pred}.  The @code{remque} routine removes @var{elem} from
its containing queue.  These routines expect to be passed pointers to
structures which have as their first members a forward pointer and a
back pointer, like this prototype (although no prototype is provided):

@example
struct qelem @{
  struct qelem *q_forw;
  struct qelem *q_back;
  char q_data[];
@};
@end example

@end deftypefn

*/


struct qelem {
  struct qelem *q_forw;
  struct qelem *q_back;
};


void
insque (struct qelem *elem, struct qelem *pred)
{
  elem -> q_forw = pred -> q_forw;
  pred -> q_forw -> q_back = elem;
  elem -> q_back = pred;
  pred -> q_forw = elem;
}


void
remque (struct qelem *elem)
{
  elem -> q_forw -> q_back = elem -> q_back;
  elem -> q_back -> q_forw = elem -> q_forw;
}
