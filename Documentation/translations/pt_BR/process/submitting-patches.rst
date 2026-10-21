.. SPDX-License-Identifier: GPL-2.0

.. _pt_BR_submittingpatches:

Enviando patches: o guia essencial para colocar o seu código no kernel
======================================================================

Para uma pessoa ou empresa que deseja enviar uma mudança para o
kernel Linux, o processo pode, por vezes, ser intimidador se você não
estiver familiarizado com "o sistema". Este texto é uma coleção de sugestões
que podem aumentar muito as chances de sua mudança ser aceita.

Este documento contém um grande número de sugestões em um formato relativamente
conciso. Para informações detalhadas sobre como funciona o processo de
desenvolvimento do kernel, consulte Documentation/process/development-process.rst.
Além disso, leia Documentation/process/submit-checklist.rst
para uma lista de itens a serem verificados antes de enviar o código.
Para patches de binding de device tree, leia
Documentation/devicetree/bindings/submitting-patches.rst.

Esta documentação assume que você está usando o ``git`` para preparar seus
patches. Se você não está familiarizado com o ``git``, é muito recomendado que
você aprenda a usá-lo, ele tornará a sua vida como um desenvolvedor do kernel e,
em geral, muito mais fácil.

Alguns subsistemas e árvores de mantenedores possuem informações adicionais
sobre seus fluxos de trabalho e expectativas, consulte
Documentation/process/maintainer-handbooks.rst.

Obtenha uma árvore de código-fonte atual
----------------------------------------

Se você não tiver um repositório com o código-fonte atual do kernel em mãos,
use o ``git`` para obter um. Você vai querer começar com o repositório mainline,
que pode ser obtido com::

  git clone git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git

Note, no entanto, que você pode não querer desenvolver diretamente na
árvore mainline. A maioria dos mantenedores de subsistemas mantém suas
próprias árvores e desejam ver os patches preparados em relação a essas árvores.
Consulte a entrada **T:** do subsistema no arquivo MAINTAINERS para encontrar
essa árvore, ou simplesmente pergunte ao mantenedor se a árvore não estiver
listada lá.

.. _pt_BR_describe_changes:

Descreva as suas mudanças
-------------------------

Descreva o seu problema. Seja o seu patch uma correção de bug de uma linha ou
5000 linhas de um novo recurso, deve haver um problema subjacente que o motivou
a fazer esse trabalho. Convença o revisor de que existe um problema que vale a
pena corrigir e que faz sentido que ele leia além do primeiro parágrafo.

Descreva o impacto visível ao usuário. Travamentos e bloqueios diretos são
bastante convincentes, mas nem todos os bugs são tão evidentes. Mesmo que o
problema tenha sido identificado durante a revisão do código, descreva o impacto
que você acredita que ele pode ter sobre os usuários. Tenha em mente que a
maioria das instalações Linux executa kernels de árvores estáveis secundárias
ou árvores específicas de fornecedores/produtos que selecionam apenas patches
específicos do upstream, então inclua qualquer coisa que possa ajudar a
direcionar sua mudança downstream: circunstâncias provocadoras, trechos do
dmesg, descrições do travamento, regressões de desempenho, picos de latência,
bloqueios, etc.

Quantifique as otimizações e compensações. Se você afirma haver melhorias no
desempenho, consumo de memória, uso da pilha ou tamanho do binário, inclua
números que as comprovem. Mas também descreva custos que não são óbvios.
Otimizações geralmente não são gratuitas, sendo trocas entre CPU, memória e
legibilidade; ou, quando se trata de heurísticas, entre diferentes cargas de
trabalho. Descreva as desvantagens esperadas da sua otimização para que o
revisor possa pesar os custos contra os benefícios.

Uma vez estabelecido o problema, descreva o que você está efetivamente fazendo
sobre ele, com detalhes técnicos. É importante descrever a mudança em inglês
claro para o revisor verificar que o código está se comportando como
você pretendia.

O mantenedor agradecerá se você escrever a descrição do seu patch em uma forma
que possa ser facilmente inserida no sistema de gerenciamento de código-fonte do
Linux, o ``git``, como uma "mensagem de commit". Veja
:ref:`pt_BR_the_canonical_patch_format`.

Resolva apenas um problema por patch. Se a sua descrição começar a ficar longa,
isso é um sinal de que você provavelmente precisa dividir o seu patch.
Consulte :ref:`pt_BR_split_changes`.

Quando você enviar ou reenviar um patch ou uma série de patches, inclua a
descrição completa do patch e a justificativa para ele. Não diga apenas
que esta é a versão N do patch (ou série). Não espere que o mantenedor
do subsistema consulte versões anteriores do patch ou URLs de referência
para encontrar a descrição do patch e colocá-la no patch.
Ou seja, o patch (ou a série) e sua descrição devem ser autossuficientes.
Isso beneficia tanto os mantenedores quanto os revisores. Alguns revisores
provavelmente nem chegaram a receber as versões anteriores do patch.

Descreva suas alterações no modo imperativo, por exemplo, "faça xyzzy executar
frotz" em vez de "[Este patch] faz xyzzy executar frotz" ou "[Eu] mudei xyzzy
para executar frotz", como se você estivesse dando ordens à base de código para
mudar o seu comportamento.

Se você quiser se referir a um commit específico, não se refira apenas ao
ID SHA-1 do commit. Por favor, inclua também o resumo de uma linha do
commit, para tornar mais fácil para os revisores saberem sobre o que se trata.
Exemplo::

	Commit e21d2170f36602ae2708 ("video: remove unnecessary
	platform_set_drvdata()") removed the unnecessary
	platform_set_drvdata(), but left the variable "dev" unused,
	delete it.

Você também deve ter a certeza de usar pelo menos os primeiros doze caracteres do
ID SHA-1. O repositório do kernel possui um número *muito* grande de objetos, o que torna as
colisões com IDs mais curtos uma possibilidade real. Tenha em mente que, mesmo que
não haja colisão com o seu ID de seis caracteres agora, essa condição pode
mudar daqui a cinco anos.

Se discussões relacionadas ou qualquer outra informação de contexto por trás da mudança
puderem ser encontradas na web, adicione tags 'Link:' apontando para isso. Se o patch é o
resultado de algumas discussões anteriores na lista de e-mails ou algo documentado na
web, aponte para ele.

Ao criar links para arquivos de listas de e-mails, de preferência use o serviço
de arquivo de mensagens lore.kernel.org. Para criar a URL do link, use o
conteúdo do cabeçalho ``Message-ID`` da mensagem, sem os colchetes angulares
circundantes. Por exemplo::

    Link: https://lore.kernel.org/30th.anniversary.repost@klaava.Helsinki.FI

Por favor, verifique o link para se certificar de que ele está realmente
funcionando e aponta para a mensagem relevante.

No entanto, tente tornar a sua explicação compreensível sem recursos
externos. Além de fornecer um URL para um arquivo da lista de e-mails ou bug,
resuma os pontos relevantes da discussão que levaram ao patch conforme enviado.

Caso o seu patch corrija um bug, use a tag 'Closes:' com um URL que referencie o
relato nos arquivos da lista de e-mails ou em um rastreador público de bugs. Por exemplo::

	Closes: https://example.com/issues/1234

Alguns rastreadores de bugs têm a capacidade de fechar os problemas
automaticamente quando um commit com tal tag é aplicado. Alguns bots que monitoram as listas de
e-mails também podem rastrear tais tags e tomar certas ações. Rastreadores de bugs
privados e URLs inválidos são proibidos.

Se o seu patch corrige um bug em um commit específico, por exemplo, você encontrou um problema usando
``git bisect``, por favor, use a tag 'Fixes:' com pelo menos os primeiros 12
caracteres do ID SHA-1 e o resumo de uma linha. Não divida a tag em
várias linhas, as tags estão isentas da regra de "quebra de linha nas 75 colunas" para
simplificar os scripts de parsing. Por exemplo::

	Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

As seguintes configurações do ``git config`` podem ser usadas para adicionar um formato aprimorado para
exibir o estilo acima nos comandos ``git log`` ou ``git show``::

	[core]
		abbrev = 12
	[pretty]
		fixes = Fixes: %h (\"%s\")

Um exemplo de chamada::

	$ git log -1 --pretty=fixes 54a4f0239f2e
	Fixes: 54a4f0239f2e ("KVM: MMU: make kvm_mmu_zap_page() return the number of pages it actually freed")

.. _pt_BR_split_changes:

Separe as suas mudanças
-----------------------

Separe cada **mudança lógica** em um patch separado.

Por exemplo, se as suas alterações incluírem tanto correções de bugs quanto melhorias
de desempenho para um único driver, separe essas alterações em dois
ou mais patches. Se as suas alterações incluírem uma atualização de API e um novo
driver que utiliza essa nova API, separe-os em dois patches.

Por outro lado, se você fizer uma única alteração em vários arquivos,
agrupe essas alterações em um único patch. Assim, uma única mudança
lógica está contida em um único patch.

O ponto a lembrar é que cada patch deve fazer uma mudança facilmente compreendida
que possa ser verificada pelos revisores. Cada patch deve ser justificável
por seus próprios méritos.

Se um patch depender de outro patch para que uma mudança seja
completa, não tem problema. Simplesmente note **"this patch depends on patch X"**
na descrição do seu patch.

Ao dividir a sua mudança em uma série de patches, tome um cuidado especial para
garantir que o kernel compile e seja executado adequadamente após cada patch da
série. Desenvolvedores que usam o ``git bisect`` para rastrear um problema podem acabar
dividindo a sua série de patches em qualquer ponto; eles não ficarão gratos se você
introduzir bugs no meio do processo.

Se você não conseguir condensar o seu conjunto de patches em um conjunto menor
de patches, então publique, digamos, apenas uns 15 de cada vez e aguarde pela
revisão e integração.



Verifique o estilo das suas mudanças
------------------------------------

Verifique o seu patch quanto a violações básicas de estilo, cujos detalhes podem ser
encontrados em Documentation/process/coding-style.rst.
Não fazer isso simplesmente desperdiça
o tempo dos revisores e fará com que o seu patch seja rejeitado, provavelmente
sem sequer ser lido.

Uma exceção significativa é quando se move código de um arquivo para
outro -- neste caso você não deve modificar o código movido no
mesmo patch que o move. Isso delineia claramente o ato de
mover o código e as suas alterações. Isso ajuda muito a revisão das
diferenças reais e permite que as ferramentas rastreiem melhor o histórico do
próprio código.

Verifique os seus patches com o verificador de estilo de patch antes de os submeter
(scripts/checkpatch.pl). Note, porém, que o verificador de estilo deve ser
visto como um guia, e não como um substituto para o julgamento humano. Se o seu
código parecer melhor com uma violação, provavelmente é melhor deixá-lo como está.

O verificador emite relatórios em três níveis:
 - ERROR: coisas que muito provavelmente estão erradas
 - WARNING: coisas que requerem uma revisão cuidadosa
 - CHECK: coisas que requerem reflexão

Você deve ser capaz de justificar todas as violações que permanecerem no seu
patch.

Selecione os destinatários do seu patch
---------------------------------------

Você deve sempre copiar o(s) mantenedor(es) e a(s) lista(s) do subsistema
apropriado(s) em qualquer patch para o código que eles mantêm; dê uma
olhada no arquivo MAINTAINERS e no histórico de revisão do código-fonte
para ver quem são esses mantenedores. O script scripts/get_maintainer.pl
pode ser muito útil nesta etapa (passe os caminhos para seus patches
como argumentos para scripts/get_maintainer.pl). Se você não conseguir
encontrar um mantenedor para o subsistema em que está trabalhando,
Andrew Morton (akpm@linux-foundation.org) serve como um mantenedor de
último recurso.

linux-kernel@vger.kernel.org deve ser usado por padrão para todos os
patches, mas o volume dessa lista fez com que vários desenvolvedores a
ignorassem. Por favor, não envie spam para listas e pessoas não
relacionadas.

Muitas listas relacionadas ao kernel estão hospedadas em kernel.org;
você pode encontrar uma lista delas em https://subspace.kernel.org.
Existem listas relacionadas ao kernel hospedadas em outros lugares
também, no entanto.

Linus Torvalds é o árbitro final de todas as mudanças aceitas no
kernel do Linux. Seu endereço de e-mail é <torvalds@linux-foundation.org>.
Ele recebe muitos e-mails e, neste momento, muito poucos patches passam
por Linus diretamente, então, normalmente, você deve fazer o seu melhor
para -evitar- enviar e-mails para ele.

Se você tiver um patch que corrija um bug de segurança explorável,
envie esse patch para security@kernel.org. Para bugs severos, um
curto embargo pode ser considerado para permitir que os distribuidores
disponibilizem o patch aos usuários; em tais casos, obviamente, o
patch não deve ser enviado a nenhuma lista pública. Veja também
Documentation/process/security-bugs.rst.

Patches que corrigem um bug severo em um kernel já lançado devem ser
direcionados aos mantenedores stable (estáveis), colocando uma linha como esta::

  Cc: stable@vger.kernel.org

na área de sign-off do seu patch (note, NÃO como um destinatário de e-mail).
Você também deve ler Documentation/process/stable-kernel-rules.rst
além deste documento.

Se as alterações afetarem as interfaces userland-kernel,
por favor, envie ao mantenedor das MAN-PAGES (como listado no arquivo MAINTAINERS)
um patch para as páginas de manual, ou pelo menos uma notificação da alteração,
para que alguma informação chegue às páginas de manual. Mudanças na API
do espaço de usuário também devem ser copiadas para linux-api@vger.kernel.org.


Sem MIME, sem links, sem compressão, sem anexos. Apenas texto puro
------------------------------------------------------------------

Linus e outros desenvolvedores do kernel precisam ser capazes de ler e
comentar as mudanças que você está enviando. É importante que um
desenvolvedor do kernel seja capaz de "citar" suas mudanças, usando
ferramentas de e-mail padrão, para que eles possam comentar em partes
específicas do seu código.

Por esse motivo, todos os patches devem ser enviados por e-mail "inline". A
maneira mais fácil de fazer isso é com ``git send-email``, que é
fortemente recomendado. Um tutorial interativo para ``git send-email``
está disponível em https://git-send-email.io.

Se você optar por não usar ``git send-email``:

.. warning::

  Tenha cuidado com a quebra de linha do seu editor corrompendo seu patch,
  se você optar por recortar e colar o seu patch.

Não anexe o patch como um anexo MIME, comprimido ou não.
Muitos aplicativos populares de e-mail nem sempre transmitirão um
anexo MIME como texto puro, tornando impossível comentar o seu
código. Um anexo MIME também leva um pouco mais de tempo para Linus
processar, diminuindo a probabilidade da sua alteração anexada em MIME
ser aceita.

Exceção: Se o seu cliente de e-mail estiver danificando os patches,
alguém pode pedir que você os reenvie usando MIME.

Veja Documentation/process/email-clients.rst para dicas sobre como
configurar seu cliente de e-mail para que ele envie seus patches intocados.

Responda aos comentários de revisão
-----------------------------------

Seu patch quase certamente receberá comentários dos revisores sobre maneiras
pelas quais o patch pode ser melhorado, na forma de uma resposta ao seu
e-mail. Você deve responder a esses comentários; ignorar revisores é uma
boa maneira de ser ignorado em troca. Você pode simplesmente responder aos
e-mails deles para responder aos seus comentários. Comentários de revisão
ou perguntas que não levam a uma alteração no código devem quase certamente
resultar em um comentário ou entrada no changelog para que o próximo
revisor entenda melhor o que está acontecendo.

Certifique-se de dizer aos revisores quais alterações você está fazendo e
de agradecê-los pelo tempo dedicado. A revisão de código é um processo
cansativo e demorado, e os revisores às vezes ficam mal-humorados. Mesmo
nesse caso, no entanto, responda educadamente e resolva os problemas que
eles apontaram. Ao enviar uma próxima versão, adicione um ``changelog do patch``
à carta de apresentação (cover letter) ou aos patches individuais,
explicando a diferença em relação ao envio anterior (veja
:ref:`pt_BR_the_canonical_patch_format`).
Notifique as pessoas que comentaram no seu patch sobre as novas versões
adicionando-as à lista de CC dos patches.

Veja Documentation/process/email-clients.rst para recomendações sobre
clientes de e-mail e etiqueta de listas de discussão.

.. _pt_BR_interleaved_replies:

Use respostas intercaladas e aparadas em discussões por e-mail
--------------------------------------------------------------
O top-posting (responder no topo) é fortemente desencorajado em
discussões de desenvolvimento do kernel do Linux. Respostas
intercaladas (ou "inline") tornam as conversas muito mais fáceis de
acompanhar. Para mais detalhes, veja:
https://en.wikipedia.org/wiki/Posting_style#Interleaved_style

Como é frequentemente citado na lista de discussão::

  A: http://en.wikipedia.org/wiki/Top_post
  Q: Onde encontro informações sobre essa coisa chamada top-posting?
  A: Porque bagunça a ordem em que as pessoas normalmente leem o texto.
  Q: Por que o top-posting é algo tão ruim?
  A: Top-posting.
  Q: Qual é a coisa mais irritante no e-mail?

Da mesma forma, por favor, apare (corte) todas as citações
desnecessárias que não são relevantes para a sua resposta. Isso torna
as respostas mais fáceis de encontrar, e economiza tempo e espaço. Para
mais detalhes, veja: http://daringfireball.net/2007/07/on_top ::

  A: Não.
  Q: Devo incluir citações após minha resposta?

.. _pt_BR_resend_reminders:

Não desanime - nem fique impaciente
-----------------------------------

Depois de ter enviado a sua alteração, seja paciente e espere. Os
revisores são pessoas ocupadas e podem não chegar ao seu patch
imediatamente.

Era uma vez, patches costumavam desaparecer no vazio sem comentários,
mas o processo de desenvolvimento funciona de forma mais suave do que
isso agora. Você deve receber comentários dentro de algumas semanas
(normalmente 2-3); se isso não acontecer, certifique-se de que você
enviou seus patches para o lugar certo. Espere por no mínimo uma
semana antes de reenviar ou dar um "ping" nos revisores -
possivelmente mais tempo durante períodos ocupados, como as janelas
de mesclagem (merge windows).

Também não há problema em reenviar o patch ou a série de patches após
algumas semanas com a palavra "RESEND" adicionada à linha de Assunto::

   [PATCH Vx RESEND] sub/sys: Resumo condensado do patch

Não adicione "RESEND" quando você estiver enviando uma versão
modificada do seu patch ou série de patches - "RESEND" se aplica
apenas ao reenvio de um patch ou série de patches que não foram
modificados de forma alguma em relação ao envio anterior.


Inclua PATCH no Assunto
-----------------------

Devido ao alto tráfego de e-mails para Linus e para a linux-kernel, é
uma convenção comum prefixar a sua linha de Assunto com [PATCH]. Isso
permite que Linus e outros desenvolvedores do kernel distingam
mais facilmente os patches de outras discussões por e-mail.

O ``git send-email`` fará isso por você automaticamente.


Assine seu trabalho - o Certificado de Origem do Desenvolvedor
--------------------------------------------------------------

Para melhorar o rastreamento de quem fez o que, especialmente com patches
que podem percolar até o seu local de descanso final no kernel através de
várias camadas de mantenedores, nós introduzimos um procedimento de
"sign-off" nos patches que estão sendo enviados por e-mail.

O sign-off é uma linha simples no final da explicação do patch, que
certifica que você o escreveu ou que de outra forma tem o direito de
repassá-lo como um patch de código aberto. As regras são bem simples:
se você pode certificar o seguinte::

        Certificado de Origem do Desenvolvedor 1.1

        Ao fazer uma contribuição para este projeto, eu certifico que:

        (a) A contribuição foi criada no todo ou em parte por mim e eu
            tenho o direito de enviá-la sob a licença de código aberto
            indicada no arquivo; ou

        (b) A contribuição baseia-se em trabalho anterior que, até onde eu
            sei, é coberto por uma licença de código aberto apropriada
            e eu tenho o direito, sob essa licença, de enviar esse
            trabalho com modificações, tenham sido criadas no todo ou
            em parte por mim, sob a mesma licença de código aberto (a menos que eu
            tenha permissão para enviar sob uma licença diferente), conforme
            indicado no arquivo; ou

        (c) A contribuição foi fornecida diretamente a mim por alguma outra
            pessoa que certificou (a), (b) ou (c) e eu não a modifiquei.

        (d) Eu entendo e concordo que este projeto e a contribuição
            são públicos e que um registro da contribuição (incluindo todas
            as informações pessoais que eu envio com ela, incluindo meu
            sign-off) é mantido indefinidamente e pode ser redistribuído de forma
            consistente com este projeto ou com a(s) licença(s) de código
            aberto envolvida(s).

então você apenas adiciona uma linha dizendo::

	Signed-off-by: Random J Developer <random@developer.example.org>

usando uma identidade conhecida (desculpe, sem contribuições anônimas.)
Isso será feito para você automaticamente se você usar o ``git commit -s``.
As reversões também devem incluir "Signed-off-by". O ``git revert -s``
faz isso por você.

Algumas pessoas também colocam tags extras no final. Elas serão
apenas ignoradas por enquanto, mas você pode fazer isso para marcar
procedimentos internos da empresa ou apenas para apontar algum
detalhe especial sobre o sign-off.

Quaisquer outros SoBs (Signed-off-by:'s) seguindo o SoB do autor
são de pessoas que manusearam e transportaram o patch, mas não
estiveram envolvidas no seu desenvolvimento. As cadeias de SoB devem
refletir a rota **real** que um patch percorreu à medida que foi
propagado aos mantenedores e, finalmente, para Linus, com a primeira
entrada de SoB sinalizando a autoria principal de um único autor.


Quando usar Acked-by:, Cc: e Co-developed-by:
---------------------------------------------

A tag Signed-off-by: indica que o signatário esteve envolvido no
desenvolvimento do patch, ou que ele/ela estava no caminho de
entrega do patch.

Se uma pessoa não esteve diretamente envolvida na preparação ou manuseio de um
patch, mas deseja manifestar e registrar sua aprovação, ela pode
pedir para ter uma linha Acked-by: adicionada ao changelog do patch.

Acked-by: destina-se a ser usado por aqueles responsáveis ou envolvidos com o
código afetado de uma forma ou de outra. Mais comumente, o mantenedor quando esse
mantenedor não contribuiu nem encaminhou o patch.

Acked-by: também pode ser usado por outras partes interessadas, como pessoas com conhecimento de
domínio (por exemplo, o autor original do código sendo modificado), revisores
do lado do espaço de usuário para um patch uAPI do kernel ou usuários-chave de um recurso. Opcionalmente,
nestes casos, pode ser útil adicionar um "# Sufixo" para esclarecer seu significado::

	Acked-by: The Stakeholder <stakeholder@example.org> # As primary user

Acked-by: não é tão formal quanto Signed-off-by:. É um registro de que o avaliador
pelo menos revisou o patch e indicou aceitação. Por isso, os responsáveis pela fusão de
patches às vezes converterão manualmente um "sim, parece bom para mim" de um avaliador
em um Acked-by: (mas note que geralmente é melhor pedir um
ack explícito).

Acked-by: também é menos formal do que Reviewed-by:. Por exemplo, mantenedores podem
usá-lo para sinalizar que estão de acordo com a inclusão de um patch, mas podem não tê-lo
revisado tão minuciosamente como se um Reviewed-by: fosse fornecido. Da mesma forma, um
usuário-chave pode não ter realizado uma revisão técnica do patch, mas ainda assim estar
satisfeito com a abordagem geral, o recurso ou a interface voltada para o usuário.

Acked-by: não indica necessariamente o reconhecimento de todo o patch.
Por exemplo, se um patch afeta vários subsistemas e tem um Acked-by: de
um mantenedor de subsistema, isso geralmente indica o reconhecimento apenas
da parte que afeta o código desse mantenedor. O bom senso deve ser usado aqui.
Em caso de dúvida, as pessoas devem consultar a discussão original nos arquivos da
lista de discussão. Um "# Sufixo" também pode ser usado neste caso para esclarecer.

Se uma pessoa teve a oportunidade de comentar em um patch, mas não
forneceu tais comentários, você pode opcionalmente adicionar uma tag ``Cc:`` ao patch.
Esta tag documenta que partes potencialmente interessadas foram incluídas na
discussão. Note que esta é uma de apenas três tags que você pode usar
sem a permissão explícita da pessoa nomeada (veja 'Marcar pessoas requer
permissão' abaixo para detalhes).

Co-developed-by: afirma que o patch foi co-criado por múltiplos desenvolvedores;
é usado para dar atribuição a coautores (além do autor
atribuído pela tag From:) quando várias pessoas trabalham em um único patch. Como
Co-developed-by: denota autoria, cada Co-developed-by: deve ser imediatamente
seguido por um Signed-off-by: do coautor associado. O procedimento padrão de assinatura
se aplica, ou seja, a ordem das tags Signed-off-by: deve refletir a
história cronológica do patch na medida do possível, independentemente se
o autor for atribuído via From: ou Co-developed-by:. Notavelmente, o último
Signed-off-by: deve ser sempre o do desenvolvedor que está enviando o patch.

Note que a tag From: é opcional quando o autor no From: também é a pessoa (e
e-mail) listada na linha From: do cabeçalho do e-mail.

Exemplo de um patch enviado pelo autor do From:::

	<changelog>

	Co-developed-by: First Co-Author <first@coauthor.example.org>
	Signed-off-by: First Co-Author <first@coauthor.example.org>
	Co-developed-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: Second Co-Author <second@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>

Exemplo de um patch enviado por um autor do Co-developed-by:::

	From: From Author <from@author.example.org>

	<changelog>

	Co-developed-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: Random Co-Author <random@coauthor.example.org>
	Signed-off-by: From Author <from@author.example.org>
	Co-developed-by: Submitting Co-Author <sub@coauthor.example.org>
	Signed-off-by: Submitting Co-Author <sub@coauthor.example.org>


Usando Reported-by:, Tested-by:, Reviewed-by:, Suggested-by: e Fixes:
---------------------------------------------------------------------

A tag Reported-by dá crédito às pessoas que encontram bugs e os relatam e
espera-se que isso as inspire a nos ajudar novamente no futuro. A tag destina-se a
bugs; por favor, não a use para dar crédito a solicitações de recursos. A tag deve ser
seguida por uma tag Closes: apontando para o relato, a menos que o relato não
esteja disponível na web. A tag Link: pode ser usada em vez de Closes: se o patch
corrigir uma parte do(s) problema(s) sendo relatado(s). Note que a tag Reported-by é
uma de apenas três tags que você pode usar sem a permissão explícita da
pessoa nomeada (veja 'Marcar pessoas requer permissão' abaixo para detalhes).

Uma tag Tested-by: indica que o patch foi testado com sucesso (em
algum ambiente) pela pessoa nomeada. Esta tag informa aos mantenedores que
algum teste foi realizado, fornece um meio para localizar testadores para
patches futuros e garante crédito para os testadores.

Reviewed-by:, por sua vez, indica que o patch foi revisado e considerado
aceitável de acordo com a Declaração do Revisor::

	Declaração de supervisão do revisor

	Ao oferecer minha tag Reviewed-by:, eu declaro que:

	 (a) Eu realizei uma revisão técnica deste patch para
	     avaliar sua adequação e prontidão para inclusão no
	     kernel mainline.

	 (b) Quaisquer problemas, preocupações ou perguntas relacionadas ao patch
	     foram comunicadas de volta ao remetente. Eu estou satisfeito
	     com a resposta do remetente aos meus comentários.

	 (c) Embora possa haver coisas que poderiam ser melhoradas com este
	     envio, eu acredito que é, neste momento, (1) uma
	     modificação que vale a pena para o kernel, e (2) livre de problemas
	     conhecidos que argumentariam contra sua inclusão.

	 (d) Embora eu tenha revisado o patch e acredite que seja sólido, eu
	     não faço (a menos que explicitamente declarado em outro lugar)
	     garantias de que alcançará seu propósito
	     declarado ou funcionará adequadamente em qualquer situação.

Uma tag Reviewed-by é uma declaração de opinião de que o patch é uma
modificação apropriada do kernel sem nenhum problema técnico sério
restante. Qualquer revisor interessado (que tenha feito o trabalho e seja uma
pessoa com identidade conhecida) pode oferecer uma tag Reviewed-by para um patch. Esta tag
serve para dar crédito aos revisores e para informar os mantenedores do grau de
revisão que foi feito no patch. Tags Reviewed-by:, quando fornecidas por
revisores conhecidos por entender a área de assunto e realizar revisões completas,
normalmente aumentarão a probabilidade de seu patch entrar no kernel.

Ambas as tags Tested-by e Reviewed-by, uma vez recebidas na lista de discussão do testador
ou revisor, devem ser adicionadas pelo autor aos patches aplicáveis ao enviar as
próximas versões. No entanto, se o patch mudou substancialmente na versão
seguinte, essas tags podem não ser mais aplicáveis e, portanto, devem ser removidas.
Normalmente, a remoção das tags Acked-by, Tested-by ou Reviewed-by de alguém deve ser
mencionada no changelog do patch com uma explicação (após o separador '---').

Uma tag Suggested-by: indica que a ideia do patch foi sugerida pela pessoa
nomeada e garante crédito à pessoa pela ideia: se creditarmos diligentemente
nossos relatores de ideias, eles serão, com sorte, inspirados a nos ajudar novamente no
futuro. Note que esta é uma de apenas três tags que você pode usar sem
permissão explícita da pessoa nomeada (veja 'Marcar pessoas requer
permissão' abaixo para detalhes).

Uma tag Fixes: indica que o patch corrige um bug em um commit anterior. Ela
é usada para facilitar a determinação de onde um problema se originou, o que pode ajudar
na revisão da correção de um bug. Esta tag também auxilia a equipe do kernel estável a determinar
quais versões do kernel estável devem receber sua correção. Este é o método preferido
para indicar um bug corrigido pelo patch. Veja :ref:`pt_BR_describe_changes`
para mais detalhes.

Nota: Anexar uma tag Fixes: não subverte o processo de regras do kernel
estável, nem o requisito de enviar em Cc: para stable@vger.kernel.org em todos os patches
candidatos estáveis. Para mais informações, por favor, leia
Documentation/process/stable-kernel-rules.rst.

Por fim, embora fornecer tags seja bem-vindo e tipicamente muito apreciado, por favor
note que os signatários (ou seja, remetentes e mantenedores) podem usar sua discrição ao
aplicar as tags oferecidas.


Marcar pessoas requer permissão
-------------------------------

Tenha cuidado ao adicionar as tags mencionadas acima aos seus patches, pois todas
exceto Cc:, Reported-by: e Suggested-by: precisam de permissão explícita da
pessoa nomeada. Para essas três, a permissão implícita é suficiente se a pessoa
contribuiu para o kernel Linux usando esse nome e endereço de e-mail de acordo
com os arquivos do lore ou o histórico de commits -- e no caso de Reported-by:
e Suggested-by: tenha feito o relato ou sugestão em público. Note que o
bugzilla.kernel.org é um local público nesse sentido, mas os endereços de e-mail
usados lá são privados; portanto, não os exponha em tags, a menos que a pessoa
os tenha usado em contribuições anteriores.

.. _pt_BR_the_canonical_patch_format:

O formato canônico do patch
---------------------------

Esta seção descreve como o próprio patch deve ser formatado. Note
que, se você tiver seus patches armazenados em um repositório ``git``, a formatação
adequada do patch pode ser obtida com ``git format-patch``. As ferramentas não podem criar
o texto necessário, no entanto, portanto, leia as instruções abaixo de qualquer maneira.

Linha de Assunto
^^^^^^^^^^^^^^^^

A linha de assunto canônica do patch é::

    Assunto: [PATCH 001/123] subsistema: frase de resumo

O corpo canônico da mensagem do patch contém o seguinte:

  - Uma linha ``from`` especificando o autor do patch, seguida por uma linha
    vazia (necessário apenas se a pessoa enviando o patch não for o autor).

  - O corpo da explicação, com quebra de linha em 75 colunas, que será
    copiado para o changelog permanente para descrever este patch.

  - Uma linha vazia.

  - As linhas ``Signed-off-by:``, descritas acima, que também
    irão para o changelog.

  - Uma linha de marcador contendo simplesmente ``---``.

  - Quaisquer comentários adicionais não adequados para o changelog.

  - O próprio patch (saída do ``diff``).

O formato da linha de Assunto torna muito fácil classificar os e-mails
alfabeticamente pela linha de assunto - praticamente qualquer leitor de e-mail
suportará isso - pois, como o número de sequência é preenchido com zeros,
a classificação numérica e alfabética é a mesma.

O ``subsystem`` no Assunto do e-mail deve identificar qual
área ou subsistema do kernel está recebendo o patch.

A ``frase de resumo`` no Assunto do e-mail deve descrever de forma concisa
o patch que esse e-mail contém. A ``frase de resumo`` não deve ser um nome de arquivo.
Não use a mesma ``frase de resumo`` para cada patch em uma série de patches inteira (onde uma ``série
de patches`` é uma sequência ordenada de múltiplos patches relacionados).

Tenha em mente que a ``frase de resumo`` do seu e-mail se torna um
identificador globalmente único para aquele patch. Ela se propaga por todo o caminho
até o changelog do ``git``. A ``frase de resumo`` pode ser usada posteriormente em
discussões de desenvolvedores que se referem ao patch. As pessoas vão querer
pesquisar no Google pela ``frase de resumo`` para ler a discussão sobre esse
patch. Também será a única coisa que as pessoas poderão ver rapidamente
quando, dois ou três meses depois, estiverem passando por talvez
milhares de patches usando ferramentas como ``gitk`` ou ``git log
--oneline``.

Por essas razões, o ``resumo`` não deve ter mais de 70-75
caracteres, e deve descrever tanto o que o patch altera, quanto
por que o patch pode ser necessário. É um desafio ser
sucinto e descritivo, mas é isso que um resumo bem escrito
deve fazer.

A ``frase de resumo`` pode ser prefixada por tags delimitadas por colchetes
retos: "Assunto: [PATCH <tag>...] <frase de resumo>". As tags não
são consideradas parte da frase de resumo, mas descrevem como o patch
deve ser tratado. Tags comuns podem incluir um descritor de versão se
as múltiplas versões do patch tiverem sido enviadas em resposta a
comentários (ou seja, "v1, v2, v3"), ou "RFC" para indicar um pedido de
comentários.

Se houver quatro patches em uma série de patches, os patches individuais podem
ser numerados assim: 1/4, 2/4, 3/4, 4/4. Isso garante que os desenvolvedores
entendam a ordem na qual os patches devem ser aplicados e que
eles tenham revisado ou aplicado todos os patches na série de patches.

Aqui estão alguns bons exemplos de Assuntos::

    Subject: [PATCH 2/5] ext2: improve scalability of bitmap searching
    Subject: [PATCH v2 01/27] x86: fix eflags tracking
    Subject: [PATCH v2] sub/sys: Condensed patch summary
    Subject: [PATCH v2 M/N] sub/sys: Condensed patch summary

Linha From
^^^^^^^^^^

A linha ``from`` deve ser a primeira linha no corpo da mensagem,
e tem a forma:

        From: Patch Author <author@example.com>

A linha ``from`` especifica quem será creditado como o autor do
patch no changelog permanente. Se a linha ``from`` estiver faltando,
então a linha ``From:`` do cabeçalho do e-mail será usada para determinar
o autor do patch no changelog.

O autor pode indicar sua afiliação ou o patrocinador do trabalho
adicionando o nome de uma organização às linhas ``from`` e ``SoB``,
por exemplo:

	From: Patch Author (Company) <author@example.com>

Corpo da Explicação
^^^^^^^^^^^^^^^^^^^

O corpo da explicação será commitado no changelog
permanente da fonte, então deve fazer sentido para um leitor competente que já
esqueceu há muito tempo os detalhes imediatos da discussão que podem ter levado a
este patch. Incluir sintomas da falha que o patch aborda
(mensagens de log do kernel, mensagens oops, etc.) é especialmente útil para
pessoas que possam estar pesquisando nas mensagens de commit procurando pelo patch
aplicável. O texto deve ser escrito com detalhes suficientes para que, quando lido
semanas, meses ou até anos depois, possa dar ao leitor os detalhes
necessários para compreender o raciocínio do **por que** o patch foi criado.

Se um patch corrige uma falha de compilação, pode não ser necessário incluir
_todas_ as falhas de compilação; apenas o suficiente para que seja provável que
alguém pesquisando pelo patch possa encontrá-lo. Como na ``frase de resumo``,
é importante ser tanto sucinto quanto descritivo.

.. _pt_BR_backtraces:

Backtraces em mensagens de commit
"""""""""""""""""""""""""""""""""

Backtraces ajudam a documentar a cadeia de chamadas que leva a um problema. No entanto,
nem todos os backtraces são úteis. Por exemplo, as cadeias de chamadas iniciais de boot são
únicas e óbvias. Copiar a saída dmesg completa verbatim, no entanto,
adiciona informações que distraem, como timestamps, listas de módulos, dumps de
registradores e pilhas.

Portanto, os backtraces mais úteis devem destilar as informações
relevantes do dump, o que facilita o foco no problema
real. Aqui está um exemplo de um backtrace bem aparado::

  unchecked MSR access error: WRMSR to 0xd51 (tried to write 0x0000000000000064)
  at rIP: 0xffffffffae059994 (native_write_msr+0x4/0x20)
  Call Trace:
  mba_wrmsr
  update_domains
  rdtgroup_mkdir

Comentários
^^^^^^^^^^^

A linha marcadora ``---`` serve ao propósito essencial de marcar para
as ferramentas de manipulação de patches onde a mensagem do changelog termina.

Um bom uso para os comentários adicionais após o marcador ``---`` é
para um ``diffstat``, para mostrar quais arquivos mudaram, e o número de
linhas inseridas e excluídas por arquivo. Um ``diffstat`` é especialmente útil
em patches maiores. Se você for incluir um ``diffstat`` após o
marcador ``---``, por favor, use as opções do ``diffstat`` ``-p 1 -w 70`` para que
os nomes dos arquivos sejam listados a partir do topo da árvore de código-fonte do kernel e não
usem muito espaço horizontal (cabem facilmente em 80 colunas, talvez com algum
recuo). (o ``git`` gera diffstats apropriados por padrão.)

Outros comentários relevantes apenas para o momento ou para o mantenedor, não
adequados para o changelog permanente, também devem ir aqui. Um bom
exemplo de tais comentários podem ser ``changelogs do patch`` que descrevem
o que mudou entre as versões v1 e v2 do patch.

Por favor, coloque esta informação **após** a linha ``---`` que separa
o changelog do restante do patch. A informação da versão não
faz parte do changelog que é commitado na árvore git. É
informação adicional para os revisores. Se for colocada acima das
tags de commit, precisará de interação manual para removê-la. Se estiver abaixo
da linha separadora, ela é automaticamente removida ao aplicar o
patch. Se disponíveis, adicionar links para as versões anteriores do patch (por exemplo,
link do arquivo lore.kernel.org) é recomendado para ajudar os revisores::

  <commit message>
  ...
  Signed-off-by: Author <author@mail>
  ---
  V2 -> V3: Removed redundant helper function
  V1 -> V2: Cleaned up coding style and addressed review comments

  v2: https://lore.kernel.org/bar
  v1: https://lore.kernel.org/foo

  path/to/file | 5+++--
  ...

Veja mais detalhes sobre o formato de patch adequado nas seguintes
referências.


Cabeçalhos In-Reply-To explícitos
---------------------------------

Pode ser útil adicionar manualmente cabeçalhos In-Reply-To: a um patch
(por exemplo, ao usar ``git send-email``) para associar o patch com
discussões relevantes anteriores, por exemplo, para vincular uma correção de bug ao e-mail com
o relatório do bug. No entanto, para uma série de múltiplos patches, geralmente é
melhor evitar usar In-Reply-To: para vincular a versões mais antigas da
série. Desta forma, múltiplas versões do patch não se tornam uma
floresta incontrolável de referências nos clientes de e-mail. Se um link for
útil, você pode usar o redirecionador https://lore.kernel.org/ (por exemplo, no
texto do e-mail de capa) para vincular a uma versão anterior da série de patches.


Informações sobre a árvore base
-------------------------------

Quando outros desenvolvedores recebem seus patches e iniciam o processo de revisão,
é absolutamente necessário que eles saibam qual é o commit/branch
base no qual seu trabalho se aplica, considerando a enorme quantidade de
árvores de mantenedores presentes hoje em dia. Note novamente a entrada **T:** no
arquivo MAINTAINERS explicado acima.

Isso é ainda mais importante para processos automatizados de CI que tentam
executar uma série de testes a fim de estabelecer a qualidade da sua
submissão antes que o mantenedor inicie a revisão.

Se você estiver usando ``git format-patch`` para gerar seus patches, você pode
incluir automaticamente as informações da árvore base em sua submissão ao
usar a flag ``--base``. A maneira mais fácil e conveniente de usar
esta opção é com branches de tópicos (topical branches)::

    $ git checkout -t -b my-topical-branch master
    Branch 'my-topical-branch' set up to track local branch 'master'.
    Switched to a new branch 'my-topical-branch'

    [perform your edits and commits]

    $ git format-patch --base=auto --cover-letter -o outgoing/ master
    outgoing/0000-cover-letter.patch
    outgoing/0001-First-Commit.patch
    outgoing/...

Quando você abrir ``outgoing/0000-cover-letter.patch`` para edição, você
notará que ele terá o trailer ``base-commit:`` bem no
final, o qual fornece ao revisor e às ferramentas de CI informações suficientes
para realizar o ``git am`` adequadamente sem se preocupar com conflitos::

    $ git checkout -b patch-review [base-commit-id]
    Switched to a new branch 'patch-review'
    $ git am patches.mbox
    Applying: First Commit
    Applying: ...

Por favor, veja ``man git-format-patch`` para mais informações sobre esta
opção.

.. note::

    A funcionalidade ``--base`` foi introduzida no git versão 2.9.0.

Se você não estiver usando git para formatar seus patches, você ainda pode incluir
o mesmo trailer ``base-commit`` para indicar o hash do commit da árvore
na qual seu trabalho se baseia. Você deve adicioná-lo na cover
letter (carta de apresentação) ou no primeiro patch da série e ele deve ser colocado
abaixo da linha ``---`` ou bem no final de todo o outro
conteúdo, logo antes da sua assinatura de e-mail.

Certifique-se de que o commit base está em uma árvore oficial de mantenedor/mainline
e não em alguma árvore interna acessível apenas por você - caso contrário seria
inútil.

Ferramentas
-----------

Muitos dos aspectos técnicos deste processo podem ser automatizados usando
b4, documentado em <https://b4.docs.kernel.org/en/latest/>. Isso pode
ajudar com coisas como rastreamento de dependências, execução do checkpatch e
com a formatação e o envio de e-mails.

Referências
-----------

Andrew Morton, "The perfect patch" (tpp).
  <https://www.ozlabs.org/~akpm/stuff/tpp.txt>

Jeff Garzik, "Linux kernel patch submission format".
  <https://web.archive.org/web/20180829112450/http://linux.yyz.us/patch-format.html>

Greg Kroah-Hartman, "How to piss off a kernel subsystem maintainer".
  <http://www.kroah.com/log/linux/maintainer.html>

  <http://www.kroah.com/log/linux/maintainer-02.html>

  <http://www.kroah.com/log/linux/maintainer-03.html>

  <http://www.kroah.com/log/linux/maintainer-04.html>

  <http://www.kroah.com/log/linux/maintainer-05.html>

  <http://www.kroah.com/log/linux/maintainer-06.html>

Kernel Documentation/process/coding-style.rst

Linus Torvalds's mail on the canonical patch format:
  <https://lore.kernel.org/r/Pine.LNX.4.58.0504071023190.28951@ppc970.osdl.org>

Andi Kleen, "On submitting kernel patches"
  Some strategies to get difficult or controversial changes in.

  http://halobates.de/on-submitting-patches.pdf
