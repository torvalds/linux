.. include:: ../disclaimer-ita.rst

:Original: :doc:`../../../process/email-clients`
:Translator: Alessia Mantegazza <amantegazza@vaga.pv.it>

.. _it_email_clients:

Informazioni sui programmi di posta elettronica per Linux
=========================================================

Git
---

Oggigiorno, la maggior parte degli sviluppatori utilizza ``git send-email``
al posto dei classici programmi di posta elettronica.  Le pagine man sono
abbastanza buone. Dal lato del ricevente, i manutentori utilizzano ``git am``
per applicare le patch.

Se siete dei novelli utilizzatori di ``git`` allora inviate la patch a voi
stessi. Salvatela come testo includendo tutte le intestazioni. Poi eseguite
il comando ``git am messaggio-formato-testo.txt`` e revisionatene il risultato
con ``git log``. Quando tutto funziona correttamente, allora potete inviare
la patch alla lista di discussione più appropriata.

Panoramica delle opzioni
------------------------

Le patch per il kernel vengono inviate per posta elettronica, preferibilmente
come testo integrante del messaggio.  Alcuni manutentori accettano gli
allegati, ma in questo caso gli allegati devono avere il *content-type*
impostato come ``text/plain``.  Tuttavia, generalmente gli allegati non sono
ben apprezzati perché rende più difficile citare porzioni di patch durante il
processo di revisione.

Inoltre, è vivamente raccomandato l'uso di puro testo nel corpo del
messaggio, sia per la patch che per qualsiasi altro messaggio. Il sito
https://useplaintext.email/ può esservi d'aiuto per configurare il
vostro programma di posta elettronica.

I programmi di posta elettronica che vengono usati per inviare le patch per il
kernel Linux dovrebbero inviarle senza alterazioni.  Per esempio, non
dovrebbero modificare o rimuovere tabulazioni o spazi, nemmeno all'inizio o
alla fine delle righe.

Non inviate patch con ``format=flowed``.  Questo potrebbe introdurre
interruzioni di riga inaspettate e indesiderate.

Non lasciate che il vostro programma di posta vada a capo automaticamente.
Questo può corrompere le patch.

I programmi di posta non dovrebbero modificare la codifica dei caratteri nel
testo.  Le patch inviate per posta elettronica dovrebbero essere codificate in
ASCII o UTF-8.
Se configurate il vostro programma per inviare messaggi codificati con UTF-8
eviterete possibili problemi di codifica.

I programmi di posta dovrebbero generare e mantenere le intestazioni
"References" o "In-Reply-To:" cosicché la discussione non venga interrotta.

Di solito, il copia-e-incolla (o taglia-e-incolla) non funziona con le patch
perché le tabulazioni vengono convertite in spazi.  Usando xclipboard, xclip
e/o xcutsel potrebbe funzionare, ma è meglio che lo verifichiate o meglio
ancora: non usate il copia-e-incolla.

Non usate firme PGP/GPG nei messaggi che contengono delle patch.  Questo
impedisce il corretto funzionamento di alcuni script per leggere o applicare
patch (questo si dovrebbe poter correggere).

Prima di inviare le patch sulle liste di discussione Linux, può essere una
buona idea quella di inviare la patch a voi stessi, salvare il messaggio
ricevuto, e applicarlo ai sorgenti con successo.


Alcuni suggerimenti per i programmi di posta elettronica (MUA)
--------------------------------------------------------------

Qui troverete alcuni suggerimenti per configurare i vostri MUA allo scopo
di modificare ed inviare patch per il kernel Linux.  Tuttavia, questi
suggerimenti non sono da considerarsi come un riassunto di una configurazione
completa.

Legenda:

- TUI = interfaccia utente testuale (*text-based user interface*)
- GUI = interfaccia utente grafica (*graphical user interface*)

Alpine (TUI)
************

Opzioni per la configurazione:

Nella sezione :menuselection:`Sending Preferences`:

- :menuselection:`Do Not Send Flowed Text` deve essere ``enabled``
- :menuselection:`Strip Whitespace Before Sending` deve essere ``disabled``

Quando state scrivendo un messaggio, il cursore dev'essere posizionato
dove volete che la patch inizi, poi premendo :kbd:`CTRL-R` vi verrà chiesto
di selezionare il file patch da inserire nel messaggio.

Claws Mail (GUI)
****************

Funziona. Alcune persone riescono ad usarlo con successo per inviare le patch.

Per inserire una patch usate :menuselection:`Messaggio-->Inserisci file`
(:kbd:`CTRL-I`) oppure un editor esterno.

Se la patch che avete inserito dev'essere modificata usato la finestra di
scrittura di Claws, allora assicuratevi che l'"auto-interruzione" sia
disabilitata :menuselection:`Configurazione-->Preferenze-->Composizione-->Interruzione riga`.

Evolution (GUI)
***************

Alcune persone riescono ad usarlo con successo per inviare le patch.

Quando state scrivendo una lettera selezionate: Preformattato
  da :menuselection:`Formato-->Stile del paragrafo-->Preformattato`
  (:kbd:`CTRL-7`) o dalla barra degli strumenti

Poi per inserire la patch usate:
:menuselection:`Inserisci--> File di testo...` (:kbd:`ALT-N x`)

Potete anche eseguire ``diff -Nru old.c new.c | xclip``, selezionare
:menuselection:`Preformattato`, e poi usare il tasto centrale del mouse.

Kmail (GUI)
***********

Alcune persone riescono ad usarlo con successo per inviare le patch.

La configurazione base che disabilita la composizione di messaggi HTML è
corretta; non abilitatela.

Quando state scrivendo un messaggio, nel menu opzioni, togliete la selezione a
"A capo automatico". L'unico svantaggio sarà che qualsiasi altra cosa scriviate
nel messaggio non verrà mandata a capo in automatico ma dovrete farlo voi.
Il modo più semplice per ovviare a questo problema è quello di scrivere il
messaggio con l'opzione abilitata e poi di salvarlo nelle bozze. Riaprendo ora
il messaggio dalle bozze le andate a capo saranno parte integrante del
messaggio, per cui togliendo l'opzione "A capo automatico" non perderete nulla.

Alla fine del vostro messaggio, appena prima di inserire la vostra patch,
aggiungete il delimitatore di patch: tre trattini (``---``).

Ora, dal menu :menuselection:`Messaggio`, selezionate :menuselection:`Inserisci file di testo...`
quindi scegliete la vostra patch.
Come soluzione aggiuntiva potreste personalizzare la vostra barra degli
strumenti aggiungendo un'icona per :menuselection:`Inserisci file di testo...`.

Allargate la finestra di scrittura abbastanza da evitare andate a capo.
Questo perché in Kmail 1.13.5 (KDE 4.5.4), Kmail aggiunge andate a capo
automaticamente al momento dell'invio per tutte quelle righe che graficamente,
nella vostra finestra di composizione, si sono estete su una riga successiva.
Disabilitare l'andata a capo automatica non è sufficiente. Dunque, se la vostra
patch contiene delle righe molto lunghe, allora dovrete allargare la finestra
di composizione per evitare che quelle righe vadano a capo. Vedere:
https://bugs.kde.org/show_bug.cgi?id=174034

Potete firmare gli allegati con GPG, ma per le patch si preferisce aggiungerle
al testo del messaggio per cui non usate la firma GPG.  Firmare le patch
inserite come testo del messaggio le rende più difficili da estrarre dalla loro
codifica a 7-bit.

Se dovete assolutamente inviare delle patch come allegati invece di integrarle
nel testo del messaggio, allora premete il tasto destro sull'allegato e
selezionate :menuselection:`Proprietà`, e poi attivate
:menuselection:`Suggerisci visualizzazione automatica` per far si che
l'allegato sia più leggibile venendo visualizzato come parte del messaggio.

Per salvare le patch inviate come parte di un messaggio, selezionate il
messaggio che la contiene, premete il tasto destro e selezionate
:menuselection:`Salva come`. Se il messaggio fu ben preparato, allora potrete
usarlo interamente senza alcuna modifica.
I messaggi vengono salvati con permessi di lettura-scrittura solo per l'utente,
nel caso in cui vogliate copiarli altrove per renderli disponibili ad altri
gruppi o al mondo, ricordatevi di usare ``chmod`` per cambiare i permessi.

Lotus Notes (GUI)
*****************

Scappate finché potete.

IBM Verse (Web GUI)
*******************

Vedi il commento per Lotus Notes.

Mutt (TUI)
**********

Un sacco di sviluppatori Linux usano ``mutt``, per cui deve funzionare
abbastanza bene.

Mutt non ha un proprio editor, quindi qualunque sia il vostro editor dovrete
configurarlo per non aggiungere automaticamente le andate a capo.  Molti
editor hanno un'opzione :menuselection:`Inserisci file` che inserisce il
contenuto di un file senza alterarlo.

Per usare ``vim`` come editor per mutt::

  set editor="vi"

Se per inserire la patch nel messaggio usate xclip, scrivete il comando::

  :set paste

prima di premere il tasto centrale o shift-insert. Oppure usate il
comando::

  :r filename

(a)llega funziona bene senza ``set paste``

Potete generare le patch con ``git format-patch`` e usare Mutt per inviarle::

    $ mutt -H 0001-some-bug-fix.patch

Opzioni per la configurazione:

Tutto dovrebbe funzionare già nella configurazione base.
Tuttavia, è una buona idea quella di impostare ``send_charset``::

   set send_charset="us-ascii:utf-8"

Mutt è molto personalizzabile. Qui di seguito trovate la configurazione minima
per iniziare ad usare Mutt per inviare patch usando Gmail::

  # .muttrc
  # ================  IMAP ====================
  set imap_user = 'yourusername@gmail.com'
  set imap_pass = 'yourpassword'
  set spoolfile = imaps://imap.gmail.com/INBOX
  set folder = imaps://imap.gmail.com/
  set record="imaps://imap.gmail.com/[Gmail]/Sent Mail"
  set postponed="imaps://imap.gmail.com/[Gmail]/Drafts"
  set mbox="imaps://imap.gmail.com/[Gmail]/All Mail"

  # ================  SMTP  ====================
  set smtp_url = "smtp://username@smtp.gmail.com:587/"
  set smtp_pass = $imap_pass
  set ssl_force_tls = yes # Require encrypted connection

  # ================  Composition  ====================
  set editor = `echo \$EDITOR`
  set edit_headers = yes  # See the headers when editing
  set charset = UTF-8     # value of $LANG; also fallback for send_charset
  # Sender, email address, and sign-off line must match
  unset use_domain        # because joe@localhost is just embarrassing
  set realname = "YOUR NAME"
  set from = "username@gmail.com"
  set use_from = yes

La documentazione di Mutt contiene molte più informazioni:

    https://gitlab.com/muttmua/mutt/-/wikis/UseCases/Gmail

    http://www.mutt.org/doc/manual/

Pine (TUI)
**********

Pine aveva alcuni problemi con gli spazi vuoti, ma questi dovrebbero essere
stati risolti.

Se potete usate alpine (il successore di pine).

Opzioni di configurazione:

- Nelle versioni più recenti è necessario avere ``quell-flowed-text``
- l'opzione ``no-strip-whitespace-before-send`` è necessaria

Sylpheed (GUI)
**************

- funziona bene per aggiungere testo in linea (o usando allegati)
- permette di utilizzare editor esterni
- è lento su cartelle grandi
- non farà l'autenticazione TSL SMTP su una connessione non SSL
- ha un utile righello nella finestra di scrittura
- la rubrica non comprende correttamente il nome da visualizzare e
  l'indirizzo associato

Thunderbird (GUI)
*****************

Thunderbird è un clone di Outlook a cui piace maciullare il testo, ma esistono
modi per impedirglielo.

- permettere l'uso di editor esterni:
  La cosa più semplice da fare con Thunderbird e le patch è quello di usare
  l'estensione "external editor" e di usare il vostro ``$EDITOR`` preferito per
  leggere/includere patch nel vostro messaggio.  Per farlo, scaricate ed
  installate l'estensione e aggiungete un bottone per chiamarla rapidamente
  usando :menuselection:`Visualizza-->Barra degli strumenti-->Personalizza...`;
  una volta fatto potrete richiamarlo premendo sul bottone mentre siete nella
  finestra :menuselection:`Scrivi`

  Tenete presente che "external editor" richiede che il vostro editor non
  faccia alcun fork, in altre parole, l'editor non deve ritornare prima di
  essere stato chiuso.  Potreste dover passare dei parametri aggiuntivi al
  vostro editor oppure cambiargli la configurazione.  Per esempio, usando
  gvim dovrete aggiungere l'opzione -f ``/usr/bin/gvim -f`` (Se il binario
  si trova in ``/usr/bin``) nell'apposito campo nell'interfaccia di
  configurazione di  :menuselection:`external editor`.  Se usate altri editor
  consultate il loro  manuale per sapere come configurarli.

Per rendere l'editor interno un po' più sensato, fate così:

- Modificate le impostazioni di Thunderbird per far si che non usi
  ``format=flowed``. Andate in :menuselection:`Modifica-->Preferenze-->Avanzate-->Editor di configurazione`
  per invocare il registro delle impostazioni.

- impostate ``mailnews.send_plaintext_flowed`` a ``false``

- impostate ``mailnews.wraplength`` da ``72`` a ``0``

- :menuselection:`Visualizza-->Corpo del messaggio come-->Testo semplice`

- :menuselection:`Visualizza-->Codifica del testo-->Unicode`


TkRat (GUI)
***********

Funziona. Usare "Inserisci file..." o un editor esterno.

Gmail (Web GUI)
***************

Non funziona per inviare le patch.

Il programma web Gmail converte automaticamente i tab in spazi.

Allo stesso tempo aggiunge andata a capo ogni 78 caratteri. Comunque
il problema della conversione fra spazi e tab può essere risolto usando
un editor esterno.

Un altro problema è che Gmail usa la codifica base64 per tutti quei messaggi
che contengono caratteri non ASCII. Questo include cose tipo i nomi europei.
