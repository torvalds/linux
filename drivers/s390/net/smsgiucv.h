/*
 * IUCV special message driver
 *
 * Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

int  smsg_register_callback(char *, void (*)(char *));
void smsg_unregister_callback(char *, void (*)(char *));

