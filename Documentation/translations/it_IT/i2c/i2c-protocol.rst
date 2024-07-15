=================
Il protocollo I2C
=================

Questo documento è una panoramica delle transazioni di base I2C e delle API
del kernel per eseguirli.

Spiegazione dei simboli
=======================

=============== ===========================================================
S               Condizione di avvio
P               Condizione di stop
Rd/Wr (1 bit)   Bit di lettura/scrittura. Rd vale 1, Wr vale 0.
A, NA (1 bit)   Bit di riconoscimento (ACK) e di non riconoscimento (NACK).
Addr  (7 bit)   Indirizzo I2C a 7 bit. Nota che questo può essere espanso
                per ottenere un indirizzo I2C a 10 bit.
Dati  (8 bit)   Un byte di dati.

[..]            Fra parentesi quadre i dati inviati da dispositivi I2C,
                anziché dal master.
=============== ===========================================================


Transazione semplice di invio
=============================

Implementato da i2c_master_send()::

  S Addr Wr [A] Dati [A] Dati [A] ... [A] Dati [A] P


Transazione semplice di ricezione
=================================

Implementato da i2c_master_recv()::

  S Addr Rd [A] [Dati] A [Dati] A ... A [Dati] NA P


Transazioni combinate
=====================

Implementato da i2c_transfer().

Sono come le transazioni di cui sopra, ma invece di uno condizione di stop P
viene inviata una condizione di inizio S e la transazione continua.
Un esempio di lettura di un byte, seguita da una scrittura di un byte::

  S Addr Rd [A] [Dati] NA S Addr Wr [A] Dati [A] P


Transazioni modificate
======================

Le seguenti modifiche al protocollo I2C possono essere generate
impostando questi flag per i messaggi I2C. Ad eccezione di I2C_M_NOSTART, sono
di solito necessari solo per risolvere problemi di un dispositivo:

I2C_M_IGNORE_NAK:
    Normalmente il messaggio viene interrotto immediatamente se il dispositivo
    risponde con [NA]. Impostando questo flag, si considera qualsiasi [NA] come
    [A] e tutto il messaggio viene inviato.
    Questi messaggi potrebbero comunque non riuscire a raggiungere il timeout
    SCL basso->alto.

I2C_M_NO_RD_ACK:
    In un messaggio di lettura, il bit A/NA del master viene saltato.

I2C_M_NOSTART:
    In una transazione combinata, potrebbe non essere generato alcun
    "S Addr Wr/Rd [A]".
    Ad esempio, impostando I2C_M_NOSTART sul secondo messaggio parziale
    genera qualcosa del tipo::

      S Addr Rd [A] [Dati] NA Dati [A] P

    Se si imposta il flag I2C_M_NOSTART per il primo messaggio parziale,
    non viene generato Addr, ma si genera la condizione di avvio S.
    Questo probabilmente confonderà tutti gli altri dispositivi sul bus, quindi
    meglio non usarlo.

    Questo viene spesso utilizzato per raccogliere le trasmissioni da più
    buffer di dati presenti nella memoria di sistema in qualcosa che appare
    come un singolo trasferimento verso il dispositivo I2C. Inoltre, alcuni
    dispositivi particolari lo utilizzano anche tra i cambi di direzione.

I2C_M_REV_DIR_ADDR:
    Questo inverte il flag Rd/Wr. Cioè, se si vuole scrivere, ma si ha bisogno
    di emettere una Rd invece di una Wr, o viceversa, si può impostare questo
    flag.
    Per esempio::

      S Addr Rd [A] Dati [A] Dati [A] ... [A] Dati [A] P

I2C_M_STOP:
    Forza una condizione di stop (P) dopo il messaggio. Alcuni protocolli
    simili a I2C come SCCB lo richiedono. Normalmente, non si vuole essere
    interrotti tra i messaggi di un trasferimento.
