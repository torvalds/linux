/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _LIST_H_
#define _LIST_H_

/*
 *
 *  Doubly-linked list manipulation routines.  Implemented as macros
 *  but logically these are procedures.
 *
 */

typedef DLIST_ENTRY LIST_ENTRY;
typedef DLIST_ENTRY *PLIST_ENTRY;

/* typedef struct LIST_ENTRY { */
/*   struct LIST_ENTRY * volatile Flink; */
/*   struct LIST_ENTRY * volatile Blink; */
/* } LIST_ENTRY, *PLIST_ENTRY; */



/*
 *  void
 *  InitializeListHead(
 *      PLIST_ENTRY ListHead
 *      );
 */
#define INITIALIZE_LIST_HEAD	InitializeListHead

#define InitializeListHead(ListHead) (\
    (ListHead)->Flink = (ListHead)->Blink = (ListHead))


/*
 *  bool
 *  IsListEmpty(
 *      PLIST_ENTRY ListHead
 *      );
 */
#define IS_LIST_EMPTY			IsListEmpty

#define IsListEmpty(ListHead) \
    ((ListHead)->Flink == (ListHead))


/*
 *  PLIST_ENTRY
 *  NextListEntry(
 *      PLIST_ENTRY Entry
 *      );
 */
#define	NEXT_LIST_ENTRY			NextListEntry

#define NextListEntry(Entry) \
    (Entry)->Flink


/*
 *  PLIST_ENTRY
 *  PrevListEntry(
 *      PLIST_ENTRY Entry
 *      );
 */
#define	PREV_LIST_ENTRY			PrevListEntry

#define PrevListEntry(Entry) \
    (Entry)->Blink


/*
 *  PLIST_ENTRY
 *  TopListEntry(
 *      PLIST_ENTRY ListHead
 *      );
 */
#define	TOP_LIST_ENTRY			TopListEntry

#define TopListEntry(ListHead) \
    (ListHead)->Flink



/*
 *  PLIST_ENTRY
 *  RemoveHeadList(
 *      PLIST_ENTRY ListHead
 *      );
 */

#define	REMOVE_HEAD_LIST		RemoveHeadList

#define RemoveHeadList(ListHead) \
    (ListHead)->Flink;\
    {RemoveEntryList((ListHead)->Flink)}


/*
 *  PLIST_ENTRY
 *  RemoveTailList(
 *      PLIST_ENTRY ListHead
 *      );
 */
#define	REMOVE_TAIL_LIST		RemoveTailList

#define RemoveTailList(ListHead) \
    (ListHead)->Blink;\
    {RemoveEntryList((ListHead)->Blink)}


/*
 *  void
 *  RemoveEntryList(
 *      PLIST_ENTRY Entry
 *      );
 */
#define	REMOVE_ENTRY_LIST		RemoveEntryList

#define RemoveEntryList(Entry) {\
    PLIST_ENTRY _EX_Flink = (Entry)->Flink;\
    PLIST_ENTRY _EX_Blink = (Entry)->Blink;\
    _EX_Blink->Flink = _EX_Flink;\
    _EX_Flink->Blink = _EX_Blink;\
	}


/*
 *  void
 *  AttachList(
 *      PLIST_ENTRY ListHead,
 *      PLIST_ENTRY ListEntry
 *      );
 */
#define	ATTACH_LIST		AttachList

#define AttachList(ListHead,ListEntry) {\
    PLIST_ENTRY _EX_ListHead = (ListHead);\
    PLIST_ENTRY _EX_Blink = (ListHead)->Blink;\
    (ListEntry)->Blink->Flink = _EX_ListHead;\
    _EX_Blink->Flink = (ListEntry);\
    _EX_ListHead->Blink = (ListEntry)->Blink;\
    (ListEntry)->Blink = _EX_Blink;\
    }



/*
 *  void
 *  InsertTailList(
 *      PLIST_ENTRY ListHead,
 *      PLIST_ENTRY Entry
 *      );
 */

#define	INSERT_TAIL_LIST		InsertTailList

#define InsertTailList(ListHead,Entry) {\
    PLIST_ENTRY _EX_ListHead = (ListHead);\
    PLIST_ENTRY _EX_Blink = (ListHead)->Blink;\
    (Entry)->Flink = _EX_ListHead;\
    (Entry)->Blink = _EX_Blink;\
    _EX_Blink->Flink = (Entry);\
    _EX_ListHead->Blink = (Entry);\
    }


/*
 *  void
 *  InsertHeadList(
 *      PLIST_ENTRY ListHead,
 *      PLIST_ENTRY Entry
 *      );
 */
#define	INSERT_HEAD_LIST		InsertHeadList

#define InsertHeadList(ListHead,Entry) {\
    PLIST_ENTRY _EX_ListHead = (ListHead);\
    PLIST_ENTRY _EX_Flink = (ListHead)->Flink;\
    (Entry)->Flink = _EX_Flink;\
    (Entry)->Blink = _EX_ListHead;\
    _EX_Flink->Blink = (Entry);\
    _EX_ListHead->Flink = (Entry);\
    }


/*
 *  void
 *  IterateListEntries(
 *      PLIST_ENTRY anchor,
 *      PLIST_ENTRY index,
 *		PLIST_ENTRY listp
 *      );
 */

#define	ITERATE_LIST_ENTRIES	IterateListEntries

#define IterateListEntries(anchor, index, listp) \
	(anchor) = (LIST_ENTRY *)(listp); \
	for((index) = (anchor)->Flink; (index) != (anchor); (index) = (index)->Flink)



/*
 *  PSINGLE_LIST_ENTRY
 *  PopEntryList(
 *      PSINGLE_LIST_ENTRY ListHead
 *      );
 */

#define	POP_ENTRY_LIST		PopEntryList

#define PopEntryList(ListHead) \
    (ListHead)->Next;\
    {\
        PSINGLE_LIST_ENTRY FirstEntry;\
        FirstEntry = (ListHead)->Next;\
        if (FirstEntry != NULL) {     \
            (ListHead)->Next = FirstEntry->Next;\
        }                             \
    }



/*
 *  void
 *  PushEntryList(
 *      PSINGLE_LIST_ENTRY ListHead,
 *		PSINGLE_LIST_ENTRY Entry
 *      );
 */

#define	PUSH_ENTRY_LIST			PushEntryList

#define PushEntryList(ListHead,Entry) \
    (Entry)->Next = (ListHead)->Next; \
    (ListHead)->Next = (Entry)

#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) ((type *)( \
                                                  (char *)(address) - \
                                                  (char *)(&((type *)0)->field)))
#endif /* CONTAINING_RECORD */

#endif /* _LIST_H_ */

/* EOF */
