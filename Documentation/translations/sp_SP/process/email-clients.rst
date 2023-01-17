.. include:: ../disclaimer-sp.rst

:Original: :ref:`Documentation/process/email-clients.rst <email_clients>`
:Translator: Carlos Bilbao <carlos.bilbao@amd.com>

.. _sp_email_clients:

Información de clientes de correo electrónico para Linux
========================================================

Git
---

A día de hoy, la mayoría de los desarrolladores usan ``git send-email`` en
lugar de los clientes de correo electrónico normales. La página de manual
para esto es bastante buena. En la recepción del correo, los maintainers
usan ``git am`` para aplicar los parches.

Si es usted nuevo en ``git`` entonces envíese su primer parche. Guárdelo
como texto sin formato, incluidos todos los encabezados. Ejecute ``git am raw_email.txt``
y luego revise el registro de cambios con ``git log``. Cuando eso funcione,
envíe el parche a la(s) lista(s) de correo apropiada(s).

Preferencias Generales
----------------------

Los parches para el kernel de Linux se envían por correo electrónico,
preferiblemente como texto en línea en el cuerpo del correo electrónico.
Algunos maintainers aceptan archivos adjuntos, pero entonces los archivos
adjuntos deben tener tipo de contenido ``text/plain``. Sin embargo, los
archivos adjuntos generalmente están mal vistos porque hacen que citar
partes del parche sea más difícil durante el proceso de revisión del
parche.

También se recomienda encarecidamente que utilice texto sin formato en el
cuerpo del correo electrónico, para parches y otros correos electrónicos
por igual. https://useplaintext.email puede ser útil para obtener
información sobre cómo configurar su cliente de correo electrónico
preferido, así como una lista de clientes de correo electrónico
recomendados si aún no tiene una preferencia.

Los clientes de correo electrónico que se utilizan para los parches del
kernel Linux deben enviar el texto del parche intacto. Por ejemplo, no
deben modificar ni eliminar pestañas o espacios, incluso al principio o al
final de las líneas.

No envíe parches con ``format=flowed``. Esto puede causar saltos de línea
no deseados e inesperados.

No deje que su cliente de correo electrónico ajuste automáticamente las
palabras por usted. Esto también puede corromper su parche.

Los clientes de correo electrónico no deben modificar la codificación del
de caracteres del texto. Los parches enviados por correo electrónico deben
estar en codificación ASCII o UTF-8 únicamente. Si configura su cliente de
correo electrónico para enviar correos electrónicos con codificación UTF-8,
evite algunos posibles problemas con los caracteres.

Los clientes de correo electrónico deben generar y mantener los
encabezados "References:" o "In-Reply-To:"  para que el hilo de correo no
se rompa.

Copiar y pegar (o cortar y pegar) generalmente no funciona para los
parches, porque las tabulaciones se convierten en espacios. Utilizar
xclipboard, xclip y/o xcutsel puede funcionar, pero es mejor probarlo usted
mismo o simplemente evitar copiar y pegar.

No utilice firmas PGP/GPG en el correo que contiene parches.
Esto rompe muchos scripts que leen y aplican los parches.
(Esto debería ser reparable.)

Es una buena idea enviarse un parche a sí mismo, guardar el mensaje
recibido, y aplicarlo con éxito con 'patch' antes de enviar el parche a las
listas de correo de Linux.

Algunas sugerencias para el cliente de correo electrónico (MUA)
---------------------------------------------------------------

Aquí hay algunos consejos específicos de configuración de MUA para editar y
enviar parches para el kernel de Linux. Estos no pretenden cubrir todo
detalle de configuración de los paquetes de software.

Leyenda:

- TUI = text-based user interface (interfaz de usuario basada en texto)
- GUI = graphical user interface (interfaz de usuario gráfica)

Alpine (TUI)
************

Opciones de configuración:

En la sección :menuselection:`Sending Preferences`:

- :menuselection: `Do Not Send Flowed Text` debe estar ``enabled``
- :menuselection:`Strip Whitespace Before Sending` debe estar ``disabled``

Al redactar el mensaje, el cursor debe colocarse donde el parche debería
aparecer, y luego presionando :kbd:`CTRL-R` se le permite especificar e
archivo de parche a insertar en el mensaje.

Claws Mail (GUI)
****************

Funciona. Algunos usan esto con éxito para los parches.

Para insertar un parche haga :menuselection:`Message-->Insert File` (:kbd:`CTRL-I`)
o use un editor externo.

Si el parche insertado debe editarse en la ventana de composición de Claws
"Auto wrapping" en
:menuselection:`Configuration-->Preferences-->Compose-->Wrapping` debe
permanecer deshabilitado.

Evolution (GUI)
***************

Algunos usan esto con éxito para sus parches.

Cuando escriba un correo seleccione: Preformat
  desde :menuselection:`Format-->Paragraph Style-->Preformatted` (:kbd:`CTRL-7`)
  o en la barra de herramientas

Luego haga:
:menuselection:`Insert-->Text File...` (:kbd:`ALT-N x`)
para insertar el parche.

También puede hacer ``diff -Nru old.c new.c | xclip``, seleccione
:menuselection:`Preformat`, luego pege con el boton del medio.

Kmail (GUI)
***********

Algunos usan Kmail con éxito para los parches.

La configuración predeterminada de no redactar en HTML es adecuada; no haga
cambios en esto.

Al redactar un correo electrónico, en las opciones, desmarque "word wrap".
La única desventaja es que cualquier texto que escriba en el correo
electrónico no se ajustará a cada palabra, por lo que tendrá que ajustar
manualmente el texto antes del parche. La forma más fácil de evitar esto es
redactar su correo electrónico con Word Wrap habilitado, luego guardar
como borrador. Una vez que lo vuelva a sacar de sus borradores, estará
envuelto por palabras y puede desmarcar "word wrap" sin perder el existente
texto.

En la parte inferior de su correo electrónico, coloque el delimitador de
parche de uso común antes de insertar su parche:  tres guiones (``---``).

Luego desde la opción de menu :menuselection:`Message` seleccione
:menuselection:`insert file` y busque su parche.
De forma adicional, puede personalizar el menú de la barra de herramientas
de creación de mensajes y poner el icono :menuselection:`insert file`.

Haga que la ventana del editor sea lo suficientemente ancha para que no se
envuelva ninguna línea. A partir de KMail 1.13.5 (KDE 4.5.4), KMail
aplicará ajuste de texto al enviar el correo electrónico si las líneas se
ajustan en la ventana del redactor. Tener ajuste de palabras deshabilitado
en el menú Opciones no es suficiente. Por lo tanto, si su parche tiene
líneas muy largas, debe hacer que la ventana del redactor sea muy amplia
antes de enviar el correo electrónico. Consulte: https://bugs.kde.org/show_bug.cgi?id=174034

You can safely GPG sign attachments, but inlined text is preferred for
patches so do not GPG sign them.  Signing patches that have been inserted
as inlined text will make them tricky to extract from their 7-bit encoding.

Puede firmar archivos adjuntos con GPG de forma segura, pero se prefiere el
texto en línea para parches, así que no los firme con GPG. Firmar parches
que se han insertado como texto en línea hará que sea difícil extraerlos de
su codificación de 7 bits.

Si es absolutamente necesario enviar parches como archivos adjuntos en
lugar de como texto en línea, haga clic con el botón derecho en el archivo
adjunto y seleccione :menuselection:`properties`, y luego
:menuselection:`Suggest automatic display` para hacer que el archivo
adjunto esté en línea para que sea más visible.

Al guardar parches que se envían como texto en línea, seleccione el correo
electrónico que contiene el parche del panel de la lista de mensajes, haga
clic con el botón derecho y seleccione :menuselection:`save as`.  Puede usar
todo el correo electrónico sin modificar como un parche de estar bien
compuesto. Los correos electrónicos se guardan como lectura y escritura
solo para el usuario, por lo que tendrá que cambiarlos para que sean
legibles en grupo y en todo el mundo si copia estos en otro lugar.

Notas de Lotus (GUI)
********************

Huya de este.

IBM Verse (Web GUI)
*******************

Vea notas sobre Lotus.

Mutt (TUI)
**********

Muchos desarrolladores de Linux usan ``mutt``, por lo que debe funcionar
bastante bien.

Mutt no viene con un editor, por lo que cualquier editor que use debe ser
utilizado de forma que no haya saltos de línea automáticos. La mayoría de
los editores tienen una opción :menuselection:`insert file` que inserta el
contenido de un archivo inalterado.

Para usar ``vim`` con mutt::

  set editor="vi"

Si utiliza xclip, escriba el comando::

  :set paste

antes del boton del medio o shift-insert o use::

  :r filename

si desea incluir el parche en línea.
(a)ttach (adjuntar) funciona bien sin ``set paste``.

También puedes generar parches con ``git format-patch`` y luego usar Mutt
para enviarlos::

    $ mutt -H 0001-some-bug-fix.patch

Opciones de configuración:

Debería funcionar con la configuración predeterminada.
Sin embargo, es una buena idea establecer ``send_charset`` en:

  set send_charset="us-ascii:utf-8"

Mutt es altamente personalizable. Aquí tiene una configuración mínima para
empezar a usar Mutt para enviar parches a través de Gmail::

  # .muttrc
  # ================  IMAP ====================
  set imap_user = 'suusuario@gmail.com'
  set imap_pass = 'sucontraseña'
  set spoolfile = imaps://imap.gmail.com/INBOX
  set folder = imaps://imap.gmail.com/
  set record="imaps://imap.gmail.com/[Gmail]/Sent Mail"
  set postponed="imaps://imap.gmail.com/[Gmail]/Drafts"
  set mbox="imaps://imap.gmail.com/[Gmail]/All Mail"

  # ================  SMTP  ====================
  set smtp_url = "smtp://username@smtp.gmail.com:587/"
  set smtp_pass = $imap_pass
  set ssl_force_tls = yes # Requerir conexión encriptada

  # ================  Composición  ====================
  set editor = `echo \$EDITOR`
  set edit_headers = yes  # Ver los encabezados al editar
  set charset = UTF-8     # valor de $LANG; also fallback for send_charset
  # El remitente, la dirección de correo electrónico y la línea de firma deben coincidir
  unset use_domain        # Porque joe@localhost es simplemente vergonzoso
  set realname = "SU NOMBRE"
  set from = "username@gmail.com"
  set use_from = yes

Los documentos Mutt tienen mucha más información:

    https://gitlab.com/muttmua/mutt/-/wikis/UseCases/Gmail

    http://www.mutt.org/doc/manual/

Pine (TUI)
**********

Pine ha tenido algunos problemas de truncamiento de espacios en blanco en
el pasado, pero estos todo debería estar arreglados ahora.

Use alpine (sucesor de pino) si puede.

Opciones de configuración:

- ``quell-flowed-text`` necesitado para versiones actuales
- la opción ``no-strip-whitespace-before-send`` es necesaria


Sylpheed (GUI)
**************

- Funciona bien para insertar texto (o usar archivos adjuntos).
- Permite el uso de un editor externo.
- Es lento en carpetas grandes.
- No realizará la autenticación TLS SMTP en una conexión que no sea SSL.
- Tiene una útil barra de reglas en la ventana de redacción.
- Agregar direcciones a la libreta de direcciones no las muestra
  adecuadamente.

Thunderbird (GUI)
*****************

Thunderbird es un clon de Outlook al que le gusta alterar el texto, pero
hay formas para obligarlo a comportarse.

Después de hacer las modificaciones, que incluye instalar las extensiones,
necesita reiniciar Thunderbird.

- Permitir el uso de un editor externo:

  Lo más fácil de hacer con Thunderbird y los parches es usar extensiones
  que abran su editor externo favorito.

  Aquí hay algunas extensiones de ejemplo que son capaces de hacer esto.

  - "External Editor Revived"

    https://github.com/Frederick888/external-editor-revived

    https://addons.thunderbird.net/en-GB/thunderbird/addon/external-editor-revived/

    Requiere instalar un "native messaging host".
    Por favor, lea la wiki que se puede encontrar aquí:
    https://github.com/Frederick888/external-editor-revived/wiki

  - "External Editor"

    https://github.com/exteditor/exteditor

    Para hacer esto, descargue e instale la extensión, luego abra la ventana
    :menuselection:`compose`, agregue un botón para ello usando
    :menuselection:`View-->Toolbars-->Customize...`
    luego simplemente haga clic en el botón nuevo cuando desee usar el editor
    externo.

    Tenga en cuenta que "External Editor" requiere que su editor no haga
    fork, o en otras palabras, el editor no debe regresar antes de cerrar.
    Es posible que deba pasar flags adicionales o cambiar la configuración
    de su editor. En particular, si está utilizando gvim, debe pasar la
    opción -f a gvim poniendo ``/usr/bin/gvim --nofork"`` (si el binario
    está en ``/usr/bin``) al campo del editor de texto en los ajustes
    :menuselection:`external editor`. Si está utilizando algún otro editor,
    lea su manual para saber cómo hacer esto.

Para sacarle algo de sentido al editor interno, haga esto:

- Edite sus ajustes de configuración de Thunderbird para que no utilice ``format=flowed``!
  Vaya a su ventana principal y busque el botón de su menú desplegable principal.
  :menuselection:`Main Menu-->Preferences-->General-->Config Editor...`
  para abrir el editor de registro de Thunderbird.

  - Seleccione ``mailnews.send_plaintext_flowed`` como ``false``

  - Seleccione ``mailnews.wraplength`` de ``72`` a ``0``

- ¡No escriba mensajes HTML! Acuda a la ventana principal
  :menuselection:`Main Menu-->Account Settings-->youracc@server.something-->Composition & Addressing`!
  Ahí puede deshabilitar la opción "Compose messages in HTML format".

- ¡Abra mensajes solo como texto sin formato! Acuda a la ventana principal
  :menuselection:`Main Menu-->View-->Message Body As-->Plain Text`!

TkRat (GUI)
***********

Funciona.  Utilice "Insert file..." o un editor externo.

Gmail (Web GUI)
***************

No funciona para enviar parches.

El cliente web de Gmail convierte las tabulaciones en espacios automáticamente.

Al mismo tiempo, envuelve líneas cada 78 caracteres con saltos de línea de
estilo CRLF aunque el problema de tab2space se puede resolver con un editor
externo.

Otro problema es que Gmail codificará en base64 cualquier mensaje que tenga
un carácter no ASCII. Eso incluye cosas como nombres europeos.
